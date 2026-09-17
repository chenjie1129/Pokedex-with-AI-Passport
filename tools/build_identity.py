#!/usr/bin/env python3
"""Identify the exact non-ignored source snapshot, including local edits."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


SEMVER_RE = re.compile(r'^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$')


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


def read_version(repo):
    path = repo / 'VERSION'
    if not path.is_file():
        raise ValueError('VERSION file is required')
    version = path.read_text(encoding='ascii').strip()
    if not SEMVER_RE.fullmatch(version):
        raise ValueError(f'Invalid semantic version in VERSION: {version!r}')
    if len(version) > 31:
        raise ValueError('VERSION must fit the ESP-IDF app version field')
    return version


def identity(repo):
    commit = git(repo, 'rev-parse', 'HEAD').decode().strip()
    version = read_version(repo)
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
    build_id = f'{commit[:12]}' + ('-dirty' if dirty else '')
    return dict(git_commit=commit, source_sha256=source_hash,
                dirty=dirty, version=version, build_id=build_id,
                source_files=files)


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
    macros = (
        ('git_commit', 'CITY_BUILD_GIT_COMMIT'),
        ('source_sha256', 'CITY_BUILD_SOURCE_SHA256'),
        ('version', 'CITY_BUILD_VERSION'),
        ('build_id', 'CITY_BUILD_ID'),
    )
    for key, macro in macros:
        header += f'#define {macro} {json.dumps(data[key])}\n'
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
