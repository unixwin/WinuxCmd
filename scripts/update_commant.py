import re

def remove_duplicate_copyright_comments(file_path):
    with open(file_path, "r", encoding="utf-8") as f:
        content = f.read()

    comment_pattern = re.compile(r"/\*.*?\*/", re.DOTALL)
    all_comments = comment_pattern.findall(content)

    copyright_comments = []
    for comment in all_comments:
        if "Copyright" in comment and (
            "Permission" in comment or "THE SOFTWARE IS PROVIDED" in comment
        ):
            copyright_comments.append(comment)

    if len(copyright_comments) <= 1:
        print(f"No duplicate copyright comments found in {file_path}")
        return

    print(f"Found {len(copyright_comments)} copyright comments in {file_path}")

    comments_to_remove = copyright_comments[1:]

    new_content = content
    for comment in comments_to_remove:
        escaped_comment = re.escape(comment)
        new_content = re.sub(escaped_comment, "", new_content, count=1)

    new_content = re.sub(r"\n\s*\n\s*\n+", "\n\n", new_content)
    new_content = new_content.lstrip("\n")

    with open(file_path, "w", encoding="utf-8") as f:
        f.write(new_content)

    print(
        f"Removed {len(comments_to_remove)} duplicate copyright comments from {file_path}"
    )
