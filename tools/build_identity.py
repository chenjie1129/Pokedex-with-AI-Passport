#!/usr/bin/env python3
"""Identify the exact non-ignored source snapshot, including local edits."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


def identity(repo):
    commit = git(repo, 'rev-parse', 'HEAD').decode().strip()
    names = sorted(set(git(repo, 'ls-files', '--cached', '--others',
                           '--exclude-standard', '-z').split(b'\0')) - {b''})
    digest = hashlib.sha256()
    files = []
    for raw in names:
        name = os.fsdecode(raw)
        path = repo / name
        if path.is_symlink():
            payload = os.fsencode(os.readlink(path))
            mode = b'link'
        elif path.is_file():
            payload = path.read_bytes()
            mode = b'executable' if path.stat().st_mode & 0o111 else b'file'
        elif not path.exists():
            payload, mode = b'', b'deleted'
        else:
            raise ValueError(f'Unsupported source entry: {name}')
        digest.update(raw + b'\0' + mode + b'\0' +
                      hashlib.sha256(payload).digest())
        files.append(name)
    dirty = bool(git(repo, 'status', '--porcelain', '--untracked-files=all'))
    source_hash = digest.hexdigest()
    version = f'{commit[:12]}-{source_hash[:10]}' + ('-dirty' if dirty else '')
    return dict(git_commit=commit, source_sha256=source_hash,
                dirty=dirty, version=version, source_files=files)


def write_if_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def cmake_quote(text):
    return '"' + str(text).replace('\\', '/').replace('"', '\\"').replace(';', '\\;').replace('$', '\\$') + '"'


def generate(repo, output):
    data = identity(repo)
    output.mkdir(parents=True, exist_ok=True)
    write_if_changed(output / 'build-identity.json',
                     json.dumps(data, indent=2, sort_keys=True) + '\n')
    header = '#pragma once\n'
    for key in ('git_commit', 'source_sha256', 'version'):
        header += f'#define CITY_BUILD_{key.upper()} {json.dumps(data[key])}\n'
    header += f'#define CITY_BUILD_DIRTY {int(data["dirty"])}\n'
    write_if_changed(output / 'city_build_identity.h', header)
    dependencies = [repo / name for name in data['source_files']]
    for name in ('HEAD', 'index', 'packed-refs'):
        path = Path(git(repo, 'rev-parse', '--git-path', name).decode().strip())
        dependencies.append(path if path.is_absolute() else repo / path)
    branch = subprocess.run(['git', '-C', str(repo), 'symbolic-ref', '-q', 'HEAD'],
                            capture_output=True, text=True)
    if branch.returncode == 0:
        path = Path(git(repo, 'rev-parse', '--git-path', branch.stdout.strip()).decode().strip())
        dependencies.append(path if path.is_absolute() else repo / path)
    cmake = f'set(PROJECT_VER {cmake_quote(data["version"])})\n'
    cmake += 'set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS\n'
    cmake += ''.join('  ' + cmake_quote(p) + '\n' for p in dependencies if p.exists())
    cmake += ')\n'
    write_if_changed(output / 'build-identity.cmake', cmake)
    return data


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    print(generate(args.repo.resolve(), args.output_dir.resolve())['version'])
