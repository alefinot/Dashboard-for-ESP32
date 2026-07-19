import re
import sys

def remove_comments(text):
    pattern = r'(".*?"|\'.*?\')|(/\*.*?\*/|//[^\r\n]*$)'
    def replacer(match):
        if match.group(2) is not None:
            return ""
        else:
            return match.group(1)
    result = re.sub(pattern, replacer, text, flags=re.MULTILINE | re.DOTALL)
    result = re.sub(r'\n\s*\n', '\n', result)
    return result

with open('src/main.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

stripped = remove_comments(content)

with open('src/main.cpp', 'w', encoding='utf-8') as f:
    f.write(stripped)

print("Comments stripped successfully.")
