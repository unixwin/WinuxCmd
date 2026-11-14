# SignPath 材料：产品路径签名所需 artifact 形态 + policy secret 切换

**性质**：准备材料，不执行任何 SignPath 操作、不改仓库 secret、不改
workflow。对应 niubash#727 的“签名门”（第 4 条）：DLL 正式启用必须经
SignPath release 批准；未批前即使①③全过也只维持实验形态。

**现有经验基线**：`D:\repo\_research-snap\niubash` 的
`.signpath/artifact-configuration/artifact-configuration.xml` +
`README.md` + `scripts/release/build-signpath-artifact-zip.ps1` +
`.github/workflows/release.yml` 的 `signpath-sign` 段；SignPath 官方
GitHub trusted-build 文档（见 §5）。

---

## 1. 产品路径的 artifact 形态清单

### 1.1 为什么必须是“整包 zip”，不是“只签 setup.exe”

FFI 产品路径的二进制不止安装器：

```
<package root>/
  setup.exe                                  Inno Setup 安装器（整 PE 签）
  niu.exe                                    宿主 shell
  winuxcmd/usr/bin/winuxcmd.exe              dispatcher
  winuxcmd/usr/bin/ffi/<ver>/winuxcore.dll   ← FFI 产品路径本体
  winuxcmd/usr/bin/ffi/<ver>/tiers.tsv       数据，不签
  winuxcmd/usr/bin/ffi/current.txt           指针，不签
  winuxcmd/usr/bin/*.exe                     shim faces（wpm hardlink）
  winuxcmd/opt/**                            上游 payload / DLL
```

如果 SignPath 项目里用的是“Installer zip”/installer-only 的
artifact configuration（只声明 `setup.exe`），`winuxcore.dll` 不会被
签；Windows 加载它时仍是 unsigned。**必须使用覆盖
`winuxcmd/**` 的配置（或新增一个 full-package 配置），并在提交时显式
传 `artifact-configuration-slug`。**

具体失败模式：项目里常见两个配置——`...-installer-zip`（只签
setup.exe）和 `...-full-package-zip`（签整包）。GitHub action 的
`artifact-configuration-slug` 不传时用项目默认；如果默认恰好是
installer-only，签名请求会**成功**返回（setup.exe 有签名），但
`winuxcore.dll` 仍是 unsigned。发布检查必须逐 PE 验证，而不是只看
“请求成功”。

### 1.2 zip 根布局（“Installer zip”经验的核心）

SignPath 的 GitHub trusted-build 连接器要求：`actions/upload-artifact`
生成的是 ZIP，因此 artifact configuration 的**根元素必须是
`<zip-file>`**；如果 `upload-artifact` 用 `archive: false`，则要
`skip-decompress: true`，配置根元素也要相应改成单文件类型。

关键约束：

1. **zip 内条目是“包根相对路径”，不能再套一层目录。**
   错误（多一层版本目录）：
   ```
   niubash-v1.4.4-win-x64/setup.exe
   niubash-v1.4.4-win-x64/winuxcmd/usr/bin/ffi/v1.1.20/winuxcore.dll
   ```
   正确（稳定根布局）：
   ```
   setup.exe
   niu.exe
   winuxcmd/usr/bin/ffi/v1.1.20/winuxcore.dll
   ```
   原因：`<pe-file path="setup.exe">` / `<include path="winuxcmd/**/*.dll">`
   是相对 zip 根的；多一层目录会让 `min-matches` 失败（签名请求直接
   报错），或者更糟——只匹配到部分文件。
2. **`setup.exe` 只能有一个顶层槽位。** 打包树里如果已经有
   `setup.exe`，必须拒绝生成（niubash 脚本有这条断言）。
3. **版本目录用通配符覆盖，不要在配置里写死 `v1.1.20`。**
   `winuxcmd/**/*.dll` 会覆盖 `ffi/<ver>/winuxcore.dll`；否则每次
   DLL 版本升级都要改 SignPath 配置。
4. **PE 集合的 `min-matches` 不能为 0（对 exe）**：至少要匹配到
   `winuxcmd/usr/bin/winuxcmd.exe`，这样空包会失败而不是“签了个空”。
   DLL 可以 `min-matches="0"`（没有 DLL 的包型仍可签）。
5. **硬链接家族**：wpm shim 是 payload 的 NTFS 硬链接；zip 会把它
   摊平成字节相同的独立条目。签名回写要用“签名前 SHA-256 相同”识别
   家族并重建硬链接（niubash 的 mapping JSON 契约）。
   `winuxcore.dll` 目前不是硬链接家族成员，但如果未来放进
   `winuxcmd/opt/` 再由 shim 链接，必须按同一规则处理。
6. **不要把 `tiers.tsv`/`current.txt` 放进 PE 集合**：它们不是 PE，
   也不该被 Authenticode 签；把它们排除在 `<pe-file-set>` 之外。
7. **签名范围与上游 OSS**：`winuxcmd/opt/**` 可能含 gawk/niugit 等
   上游二进制。SignPath Foundation 条款要求“上游 OSS 二进制不得用
   本订阅签名”；现有 niubash 配置签整棵 `winuxcmd/**`，这是一个
   待与 Foundation 确认的开放项（README open decision #2）。如果
   确认要排除，必须同时处理 shim 硬链接家族，不能只删 include。

### 1.3 清单（可直接勾）

- [ ] 产物是**整包**：`setup.exe` + `niu.exe` + `winuxcmd/**`。
- [ ] zip 根是**包根**，无版本目录包裹；条目名稳定。
- [ ] 配置根元素 `<zip-file>`（或 `archive:false` + 对应单文件根）。
- [ ] `winuxcmd/**/*.exe`：`min-matches=1`，`max-matches=unbounded`。
- [ ] `winuxcmd/**/*.dll`：`min-matches=0`，覆盖
      `winuxcmd/usr/bin/ffi/<ver>/winuxcore.dll`。
- [ ] `setup.exe` 单独 `<pe-file>`，整 PE 签。
- [ ] `niu.exe` 单独 `<pe-file>`。
- [ ] 不含 `tiers.tsv` / `current.txt` / 其他非 PE。
- [ ] 顶层只有一个 `setup.exe`；包树内无同名文件。
- [ ] 提交时显式 `artifact-configuration-slug`（不要依赖项目默认）。
- [ ] 签名前记录每个条目的 SHA-256（回写契约）。
- [ ] 签名后重建硬链接家族；复核 `usr/bin` 与 `opt` 字节一致。
- [ ] 签名后重新生成所有 `.sha256`、release `SHA256SUMS`、winget
      `InstallerSha256`——**digest 必须描述签名后的字节**。
- [ ] VERSIONINFO：若启用 Foundation 的 metadata restrictions，
      `winuxcore.dll` / `winuxcmd.exe` / shim 必须有产品名与版本资源。
      现有资料明确 `niu.exe`/`winuxcmd.exe`/shim 没有 VERSIONINFO
      （niubash README open decision #1）；本仓
      `resources/winuxcmd.rc` 只有 icon、没有 `VERSIONINFO`，FFI 的
      `winuxcore.dll` target 也没有资源步骤。配置里暂时不能加
      `product-name`/`product-version` 约束，需先在构建中补齐并核对
      每个 PE。
- [ ] 测试签名链与发布签名链分开验证：test 证书在 runner 上
      `Status` 可能是 `UntrustedRoot`/`UnknownError`，但绝不能是
      `NotSigned`；release 证书必须 `Status -eq Valid`。

### 1.4 与 FFI 复活的关系

- 签名门是 #727 的第 4 条，不是 #1487 的代码缺陷；但**产品路径复活
  必须同时满足**：`winuxcore.dll` 被 release 证书签、且宿主加载的是
  签名后的字节。
- 签名不改变 DLL 行为；它只解决信誉/信任。不要用“已签名”替代
  门①③ 的实证。
- 如果 DLL 版本目录随构建变化，artifact 配置用通配符；但
  `current.txt` 指向的版本必须与签名条目的版本一致，发布检查要
  交叉核对 `current.txt` ↔ zip 内 `ffi/<ver>/winuxcore.dll`。

---

## 2. policy secret 切换：test-signing → release-signing

niubash 的现有设计是“四 secret 齐备才启用签名，删一个就回到未签名”，
切换 policy **不需要改 workflow**。步骤如下（按顺序）：

### 2.1 前置确认（SignPath 侧）

1. 确认 release-signing policy 已创建并获得批准（Foundation 的
   test-certificate 阶段结束、release 证书可用）；记录 policy slug。
2. 确认 release policy 允许当前项目/artifact configuration；如果
   artifact configuration 也换了（例如从 installer-only 换成
   full-package），记录新的 `artifact-configuration-slug`。
3. 确认 CI user 在**新 policy** 上有 submitter 权限；必要时重建
   API token 并安排轮换。
4. 确认审批人/审批时限：release policy 仍可能需要人工批准；
   `wait-for-completion-timeout-in-seconds` 要覆盖审批延迟。

### 2.2 仓库 secret 切换（GitHub Settings → Secrets and variables → Actions）

先记录当前值对应的 test policy slug（GitHub secret 不可回读，必须在
SignPath UI 或变更单里留档），再改值；只改值，不改 workflow：

| Secret | 动作 |
|---|---|
| `SIGNPATH_SIGNING_POLICY_SLUG` | **改**为 release policy slug |
| `SIGNPATH_PROJECT_ID` | 通常不变；若项目重建则改 |
| `SIGNPATH_ORGANIZATION_ID`（或 `SIGNPATH_ORG_ID`） | 通常不变 |
| `SIGNPATH_API_TOKEN` | 若 CI user 权限/轮换要求变化则更新 |
| `artifact-configuration-slug` | 不是 secret；在 workflow 的 action 参数里显式传 full-package slug |

注意：niubash 的 `signpath-gate` 只检查四个 secret 非空；把 policy
slug 换成 release 后，下一次 release 就会走 release policy。**不要在
同一次发布里同时改 policy 和 artifact 形态**，否则失败时无法区分是
证书问题还是 zip 布局问题。

### 2.3 验证步骤（切换后的第一次发布）

1. 先跑 `signpath-smoke.yml`（dispatch，无输入）提交一个真实安装器；
   确认返回 `signing-request-id` 和签名后的 PE。
2. 下载签名产物，`Get-AuthenticodeSignature`：
   - test policy：允许 `UntrustedRoot`/`UnknownError`，但**不允许**
     `NotSigned`；
   - release policy：要求 `Status -eq Valid`，并记录
     `SignerCertificate.Subject`、有效期、证书链。
3. 对 full-package 产物核对：`winuxcore.dll` 在 zip 内、被签、签名
   者是 release 证书；`tiers.tsv`/`current.txt` 未被签。
4. 重新计算所有 digest（`.sha256`、`SHA256SUMS`、winget
   `InstallerSha256`），确认描述的是**签名后**字节。
5. 记录切换证据：policy slug、artifact config slug、签名请求 URL、
   证书 subject/thumbprint、审批时间、token 轮换日期。
6. 回滚预案：把 `SIGNPATH_SIGNING_POLICY_SLUG` 改回 test policy（或
   删掉任一 secret 使 `signpath-gate` 关闭）；**已用 test 证书签过的
   产物不得进入 release**，必须重签或撤回。

### 2.4 不要做的事

- 不要把 API token 写进 workflow、日志或本仓库。
- 不要跳过 `Get-AuthenticodeSignature` 的 `NotSigned` 断言。
- 不要在 policy 切换的同一次提交里修改 artifact configuration；
  分开两次，各自有 smoke 证据。
- 不要把 `winuxcore.dll` 单独提交给 SignPath：它是整包 zip 的一个
  条目；单独签会破坏包级 digest/硬链接回写契约。

---

## 3. 与 WinuxCmd 仓库的关系

- WinuxCmd 仓库当前**没有** SignPath workflow/config；签名材料应作为
  niubash 项目的一部分维护（DLL 随 niubash 包出货）。
- 如果未来 WinuxCmd 独立发布 `winuxcore.dll`，需要新建 SignPath
  project，并复制 §1.2 的 zip 根布局规则；不要复用 installer-only
  配置。
- 仓库内不得提交任何 SignPath secret；只能提交
  `artifact-configuration.xml` 与说明文档。

---

## 4. 时间/依赖估算

| 项 | 依赖 | 量级 |
|---|---|---|
| full-package artifact config + zip normalizer | 现有 niubash 脚本 | 0.5-1 人日（配置 + 本地 dry run） |
| VERSIONINFO 资源（若要 metadata restrictions） | `winuxcore.dll`/`winuxcmd.exe`/shim | 1-3 人日 + 重新构建 |
| release policy 审批 | SignPath/Foundation | 外部，日历时间不可控 |
| secret 切换 + smoke | 四 secret、release policy | 0.5 人日 + 一次真实请求 |
| 签名后 digest/硬链接回写验证 | release workflow | 0.5-1 人日 |

**结论**：签名不是 FFI 代码缺陷的替代品；它是复活门的第 4 条。
在 release policy 批下来之前，即使门①③全绿，也只能维持
`DEFAULT_ENABLED=false` 的实验形态。

---

## 5. 来源

1. `D:\repo\_research-snap\niubash\.signpath\artifact-configuration\artifact-configuration.xml`
   （`<zip-file>` 根、`setup.exe`/`niu.exe`/`winuxcmd/**` 条目）。
2. 同目录 `README.md`（四 secret、open decisions #1/#2、硬链接回写）。
3. `D:\repo\_research-snap\niubash\scripts\release\build-signpath-artifact-zip.ps1`
   （稳定条目名、根布局、mapping JSON）。
4. `D:\repo\_research-snap\niubash\scripts\release\apply-signpath-signatures.ps1`
   （签名回写与硬链接家族重建）。
5. `D:\repo\_research-snap\niubash\.github\workflows\release.yml`
   （`signpath-gate`、`github-action-submit-signing-request@v3`、
   `NotSigned` 断言、签名后 digest）。
6. `D:\repo\_research-snap\niubash\.github\workflows\signpath-smoke.yml`
   （test policy 诊断、REST 查询、org secret 双候选）。
7. SignPath docs, *Trusted Build Systems → GitHub*：
   https://docs.signpath.io/trusted-build-systems/github
   （upload-artifact 生成 ZIP ⇒ 根元素必须是 `<zip-file>`；
   `artifact-configuration-slug` 参数；`archive:false` ⇒
   `skip-decompress:true`）。
8. SignPath docs, *Artifact Configuration*：
   https://docs.signpath.io/artifact-configuration
   （上传样本、wildcards、multiple files via ZIP）。
9. WinuxCmd `DOCS/2.0-HANDOVER.md`「零、续作检查点」与 niubash#727
   第 4 条（签名门）。
