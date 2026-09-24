<#
.SYNOPSIS
    WinuxCmd One-Click Release Script
.DESCRIPTION
    Updates version numbers in all files and pushes to GitHub
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$Version,

    [Parameter(Mandatory=$false)]
    [string]$CommitMessage = "release: version $Version",

    [Parameter(Mandatory=$false)]
    [switch]$SkipCommit,

    [Parameter(Mandatory=$false)]
    [switch]$PrepareOnly,

    [Parameter(Mandatory=$false)]
    [switch]$PullRequestFlow,

    [Parameter(Mandatory=$false)]
    [string]$BaseBranch = "main",

    [Parameter(Mandatory=$false)]
    [string]$ReleaseBranch = "",

    [Parameter(Mandatory=$false)]
    [switch]$CleanupOldRemoteBranches
)

$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $true
}

function Write-Color {
    param($Color, $Text)
    $Colors = @{
        Green  = "[OK]"
        Yellow = "[INFO]"
        Red    = "[ERROR]"
        Blue   = "[NOTE]"
        Cyan   = "[INFO]"
    }
    Write-Host "$($Colors[$Color]) $Text"
}

Write-Color "Cyan" "WinuxCmd One-Click Release Script"
Write-Color "Cyan" "================================"
Write-Host ""

# Validate version format
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Color "Red" "Invalid version format. Use semantic versioning (e.g., 0.4.5)"
    exit 1
}

Write-Color "Yellow" "Version: $Version"
Write-Host ""

if ($ReleaseBranch) {
    Write-Color "Yellow" "Preparing release branch $ReleaseBranch from $BaseBranch..."
    git fetch origin $BaseBranch --prune
    $currentBranch = (git branch --show-current).Trim()
    if (-not $currentBranch) {
        Write-Color "Red" "Cannot determine current git branch"
        exit 1
    }
    if ($currentBranch -ne $ReleaseBranch) {
        $localBranchExists = git branch --list $ReleaseBranch
        if ($localBranchExists) {
            git switch $ReleaseBranch
        } else {
            $pendingChanges = git status --short
            if ($pendingChanges) {
                Write-Color "Yellow" "  Preserving pending working tree changes on new release branch"
                git switch -c $ReleaseBranch
            } else {
                git switch -c $ReleaseBranch "origin/$BaseBranch"
            }
        }
    }
    $currentBranch = (git branch --show-current).Trim()
    if ($currentBranch -ne $ReleaseBranch) {
        Write-Color "Red" "Expected to be on $ReleaseBranch, but current branch is $currentBranch"
        exit 1
    }
    Write-Color "Green" "  On release branch $ReleaseBranch"
    Write-Host ""
}

# Update version sources
Write-Color "Yellow" "Updating version metadata..."
Set-Content -Path "PROJECT_VERSION" -Value $Version -Encoding ascii -NoNewline
Write-Color "Green" "  Updated PROJECT_VERSION"

$releaseDate = Get-Date -Format "yyyy-MM-dd"
$builtinIndexVersion = "builtin-" + $releaseDate.Replace("-", ".")
$wpmPath = "src\commands\wpm.cpp"
$wpm = Get-Content $wpmPath -Raw
$wpm = $wpm -replace '"version": "builtin-\d{4}\.\d{2}\.\d{2}"',
                    ('"version": "' + $builtinIndexVersion + '"')
$wpm = $wpm -replace '"updated": "\d{4}-\d{2}-\d{2}"',
                    ('"updated": "' + $releaseDate + '"')
$wpm = $wpm -replace '"version": "\d+\.\d+\.\d+",\r?\n      "description": "WinuxCmd core command set"',
                    ('"version": "' + $Version + '",' + "`n" + '      "description": "WinuxCmd core command set"')
Set-Content -Path $wpmPath -Value $wpm -Encoding utf8 -NoNewline
Write-Color "Green" "  Updated WPM builtin index metadata"

Write-Host ""

# Check git status
$gitStatus = git status --short
if (-not $gitStatus) {
    if (-not ($PullRequestFlow -and $SkipCommit)) {
        Write-Color "Yellow" "No changes detected. Files already at version $Version"
        exit 0
    }

    Write-Color "Yellow" "No uncommitted changes detected; continuing PR flow from existing branch commit"
    Write-Host ""
}

if ($PrepareOnly) {
    Write-Color "Yellow" "Prepare-only mode: version metadata updated; skipping commit, push, and tag"
    exit 0
}

Write-Color "Yellow" "Git changes detected:"
Write-Host $gitStatus
Write-Host ""

# Commit changes
if (-not $SkipCommit) {
    Write-Color "Yellow" "Committing changes..."
    git add -A
    git commit -m $CommitMessage
    Write-Color "Green" "  Committed successfully"
    Write-Host ""
} else {
    Write-Color "Yellow" "Skipping commit (--SkipCommit specified)"
    Write-Host ""
}

if ($PullRequestFlow) {
    $currentBranch = (git branch --show-current).Trim()
    if (-not $currentBranch) {
        Write-Color "Red" "Cannot determine current git branch"
        exit 1
    }
    if ($currentBranch -eq $BaseBranch) {
        Write-Color "Red" "PullRequestFlow must run from a release/fix branch, not $BaseBranch"
        exit 1
    }

    $tagName = "v$Version"

    Write-Color "Yellow" "Pushing branch $currentBranch..."
    git push -u origin $currentBranch
    Write-Color "Green" "  Branch pushed"
    Write-Host ""

    Write-Color "Yellow" "Creating or reusing pull request into $BaseBranch..."
    $existingJson = gh pr list --head $currentBranch --base $BaseBranch --state open --json number,url
    $existing = @()
    if ($existingJson) {
        $parsedExisting = $existingJson | ConvertFrom-Json
        if ($null -ne $parsedExisting) {
            $existing = @($parsedExisting)
        }
    }
    if ($existing.Count -gt 0) {
        $prNumber = $existing[0].number
        $prUrl = $existing[0].url
        Write-Color "Green" "  Reusing PR #${prNumber}: $prUrl"
    } else {
        $bodyPath = Join-Path ([System.IO.Path]::GetTempPath()) "winuxcmd-release-$Version-pr.md"
        Set-Content -Path $bodyPath -Encoding utf8 -Value @"
## Summary
- Release WinuxCmd $Version
- Update PROJECT_VERSION and WPM builtin metadata

## Verification
- Run the repository test suite before merging this release PR.
"@
        $prUrl = (gh pr create --base $BaseBranch --head $currentBranch --title "Release v$Version" --body-file $bodyPath).Trim()
        Remove-Item -LiteralPath $bodyPath -Force -ErrorAction SilentlyContinue
        $prNumber = (($prUrl -split "/")[-1])
        Write-Color "Green" "  Created PR #${prNumber}: $prUrl"
    }
    Write-Host ""

    Write-Color "Yellow" "Waiting for pull request checks..."
    $oldNativeErrorPreference = $PSNativeCommandUseErrorActionPreference
    $oldErrorActionPreference = $ErrorActionPreference
    $PSNativeCommandUseErrorActionPreference = $false
    $ErrorActionPreference = "Continue"
    try {
        $checksOutput = gh pr checks $prNumber --watch --interval 10 2>&1
        $checksExitCode = $LASTEXITCODE
    } finally {
        $PSNativeCommandUseErrorActionPreference = $oldNativeErrorPreference
        $ErrorActionPreference = $oldErrorActionPreference
    }
    if ($checksExitCode -ne 0) {
        $checksText = ($checksOutput | Out-String)
        if ($checksText -match "no checks reported") {
            Write-Color "Yellow" "  No pull request checks reported; continuing"
        } else {
            Write-Host $checksText
            exit $checksExitCode
        }
    } else {
        Write-Color "Green" "  Checks completed"
    }
    Write-Host ""

    Write-Color "Yellow" "Merging PR #$prNumber..."
    gh pr merge $prNumber --merge --subject "Merge release v$Version" --body "Release v$Version"
    Write-Color "Green" "  PR merged"
    Write-Host ""

    Write-Color "Yellow" "Fetching merged $BaseBranch..."
    git fetch origin $BaseBranch --tags
    Write-Color "Green" "  Fetched origin/$BaseBranch"
    Write-Host ""

    $existingTag = git tag -l $tagName
    if ($existingTag) {
        Write-Color "Red" "Tag $tagName already exists locally. Refusing to overwrite."
        exit 1
    }

    Write-Color "Yellow" "Creating tag $tagName on origin/$BaseBranch..."
    git tag -a $tagName "origin/$BaseBranch" -m "Release $tagName"
    Write-Color "Green" "  Tag created"
    Write-Host ""

    Write-Color "Yellow" "Pushing tag $tagName..."
    git push origin $tagName
    Write-Color "Green" "  Tag pushed"
    Write-Host ""

    Write-Color "Yellow" "Syncing local $BaseBranch..."
    git branch -f $BaseBranch "origin/$BaseBranch"
    git switch $BaseBranch
    Write-Color "Green" "  Local $BaseBranch is up to date"
    Write-Host ""

    if ($CleanupOldRemoteBranches) {
        Write-Color "Yellow" "Cleaning old remote branches except $BaseBranch..."
        $remoteBranches = git branch -r |
            ForEach-Object { $_.Trim() } |
            Where-Object {
                $_ -and
                $_ -ne "origin/HEAD" -and
                $_ -ne "origin/$BaseBranch" -and
                $_ -notmatch "origin/HEAD ->"
            }
        foreach ($remoteBranch in $remoteBranches) {
            $branchName = $remoteBranch -replace '^origin/', ''
            Write-Color "Yellow" "  Deleting origin/$branchName"
            git push origin --delete $branchName
        }
        git fetch origin --prune
        Write-Color "Green" "  Remote branch cleanup complete"
        Write-Host ""
    }

    Write-Color "Cyan" "================================"
    Write-Color "Green" "Release $tagName completed successfully via PR flow!"
    Write-Color "Cyan" "================================"
    exit 0
}

# Push to remote
Write-Color "Yellow" "Pushing to remote..."
git push origin main
Write-Color "Green" "  Pushed successfully"
Write-Host ""

# Create tag
Write-Color "Yellow" "Creating tag v$Version..."
git tag -a "v$Version" -m "Release v$Version"
Write-Color "Green" "  Tag created"
Write-Host ""

# Push tag
Write-Color "Yellow" "Pushing tag to remote..."
git push origin "v$Version"
Write-Color "Green" "  Tag pushed"
Write-Host ""

Write-Color "Cyan" "================================"
Write-Color "Green" "Release v$Version completed successfully!"
Write-Color "Cyan" "================================"
Write-Host ""
Write-Host "Next steps:"
Write-Host "1. Monitor GitHub Actions build: https://github.com/unixwin/WinuxCmd/actions"
Write-Host "2. Check release: https://github.com/unixwin/WinuxCmd/releases/tag/v$Version"
Write-Host ""
