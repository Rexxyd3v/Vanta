"""Publish the tested Windows installer using the existing Git credential-manager sign-in."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.error
import urllib.parse
import urllib.request

REPOSITORY = 'Rexxyd3v/Vanta'
API = f'https://api.github.com/repos/{REPOSITORY}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', default='v1.0.0')
    parser.add_argument('--expected-commit', required=True)
    parser.add_argument('--publish', action='store_true', help='Publish after verified uploads; otherwise leave a draft')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    environment = dict(os.environ, GIT_TERMINAL_PROMPT='0', GCM_INTERACTIVE='never')
    credential = subprocess.run(['git', 'credential', 'fill'], input='protocol=https\nhost=github.com\n\n', text=True, capture_output=True, env=environment, check=True)
    fields = dict(line.split('=', 1) for line in credential.stdout.splitlines() if '=' in line)
    token = fields.get('password')
    if not token:
        raise RuntimeError('Sign in to GitHub using Git Credential Manager first.')

    def request(url, method='GET', body=None, binary=None):
        headers = {'Authorization': 'Bearer ' + token, 'User-Agent': 'Vanta-release-packager', 'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'}
        if binary:
            headers.update({'Content-Type': 'application/octet-stream', 'Content-Length': str(binary.stat().st_size)})
            with binary.open('rb') as stream:
                req = urllib.request.Request(url, data=stream, headers=headers, method=method)
                with urllib.request.urlopen(req, timeout=300) as response:
                    return json.load(response)
        data = None if body is None else json.dumps(body).encode()
        if data is not None:
            headers['Content-Type'] = 'application/json'
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        with urllib.request.urlopen(req, timeout=60) as response:
            return json.load(response)

    tag = urllib.parse.quote(args.tag, safe='')
    reference = request(API + '/git/ref/tags/' + tag)['object']
    while reference['type'] == 'tag':
        reference = request(API + '/git/tags/' + reference['sha'])['object']
    if reference['sha'] != args.expected_commit:
        raise RuntimeError('The remote release tag does not match the tested source commit.')
    version = args.tag.removeprefix('v')
    folder = root / 'bin' / 'release' / version
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8-sig'))
    if manifest['sourceCommit'] != args.expected_commit:
        raise RuntimeError('Package manifest and release source commit differ.')
    assets = [folder / f'VANTA-{version}-Setup.exe', folder / 'SHA256SUMS.txt']
    for asset in assets:
        if not asset.is_file() or not asset.stat().st_size:
            raise RuntimeError(f'Missing release asset: {asset.name}')
    try:
        release = request(API + '/releases/tags/' + tag)
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        release = request(API + '/releases', 'POST', {'tag_name': args.tag, 'target_commitish': args.expected_commit, 'name': f'VANTA {version}', 'body': (root / 'packaging/RELEASE_NOTES.md').read_text(encoding='utf-8-sig'), 'draft': True, 'prerelease': False})
    for asset in assets:
        digest = hashlib.sha256(asset.read_bytes()).hexdigest()
        existing = next((item for item in release['assets'] if item['name'] == asset.name), None)
        if existing:
            if existing.get('digest') != 'sha256:' + digest:
                raise RuntimeError(f'An existing download differs: {asset.name}. It was not overwritten.')
            print('Verified existing asset:', asset.name, flush=True)
            continue
        upload = release['upload_url'].split('{', 1)[0] + '?name=' + urllib.parse.quote(asset.name)
        print('Uploading:', asset.name, asset.stat().st_size, 'bytes', flush=True)
        item = request(upload, 'POST', binary=asset)
        if item['state'] != 'uploaded' or item['size'] != asset.stat().st_size:
            raise RuntimeError('GitHub did not confirm the complete upload.')
        if item.get('digest') and item['digest'] != 'sha256:' + digest:
            raise RuntimeError('The uploaded file checksum differs.')
        print('Uploaded:', item['browser_download_url'], flush=True)
    if args.publish:
        release = request(API + '/releases/' + str(release['id']), 'PATCH', {'draft': False, 'make_latest': 'true'})
    print(('Published: ' if not release['draft'] else 'Draft ready: ') + release['html_url'], flush=True)


if __name__ == '__main__':
    try:
        main()
    except urllib.error.HTTPError as error:
        print(f'GitHub API request failed: HTTP {error.code}')
        raise SystemExit(1)
