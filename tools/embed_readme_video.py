"""Replace the README preview with an uploaded GitHub video attachment URL."""
import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('attachment_url', help='URL returned by GitHub after uploading the MP4')
    args = parser.parse_args()
    if not re.fullmatch(r'https://github\.com/user-attachments/assets/[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}', args.attachment_url):
        parser.error('Use the actual https://github.com/user-attachments/assets/<UUID> URL from the GitHub editor.')
    readme = Path(__file__).resolve().parents[1] / 'README.md'
    text = readme.read_text(encoding='utf-8-sig')
    start, end = '<!-- GAMEPLAY_VIDEO_START -->', '<!-- GAMEPLAY_VIDEO_END -->'
    if text.count(start) != 1 or text.count(end) != 1 or text.index(start) > text.index(end):
        parser.error('README must contain exactly one ordered GAMEPLAY_VIDEO_START/END marker pair.')
    before, rest = text.split(start)
    _, after = rest.split(end)
    readme.write_text(before + start + '\n' + args.attachment_url + '\n' + end + after, encoding='utf-8')
    print('README now embeds the uploaded GitHub video. Commit/push README.md to publish it.')


if __name__ == '__main__':
    main()
