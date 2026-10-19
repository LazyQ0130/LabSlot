"""Copy the two production executables to a confirmed, new demonstration directory.

Never writes business data, accounts or maintenance logs; never removes anything.
"""
import argparse
from datetime import date, timedelta
from pathlib import Path
import shutil
import stat


def prepare(target, confirmed=False):
    root = Path(__file__).resolve().parents[1]
    requested = Path(target).expanduser()
    if not requested.is_absolute():
        raise ValueError('Target must be an absolute path.')
    for part in (requested, *requested.parents):
        if part.exists() and (part.is_symlink() or
                             getattr(part.lstat(), 'st_file_attributes', 0) &
                             getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400)):
            raise ValueError('Target and its parents must not be links or reparse points.')
    target = requested.resolve()
    if target == root / 'data' or root / 'data' in target.parents:
        raise ValueError('The development data directory is protected.')
    if target.exists():
        raise ValueError('Target already exists. Choose a new directory; nothing will be replaced.')
    if not target.parent.is_dir():
        raise ValueError('Target parent must already exist.')
    binaries = [root / 'LabSlot-GUI.exe', root / 'smart-lab.exe']
    if not all(source.is_file() for source in binaries):
        raise ValueError('Build both production executables first with build.ps1.')
    print('New isolated demonstration directory:', target)
    if not confirmed:
        print('No data or accounts will be copied. Type the full target path to confirm:')
        try:
            response = input().strip()
        except EOFError:
            response = ''
        if response != str(target):
            raise ValueError('Initialization was not confirmed; no directory was created.')
    target.mkdir()  # Atomic refusal if another process created the target meanwhile.
    for source in binaries:
        shutil.copy2(source, target / source.name)
    tomorrow = date.today() + timedelta(days=1)
    (target / 'DEMO_SESSION.md').write_text(
        '# Isolated LabSlot demonstration\n\n'
        f'Prepared on {date.today():%Y-%m-%d}. Suggested booking: tomorrow '
        f'({tomorrow:%Y-%m-%d}), 08:00-09:00. Recheck the real date before presenting.\n\n'
        'Start LabSlot-GUI.exe here. The production program initializes its own data/ '
        'and uses the real Windows clock. Register synthetic student and admin accounts '
        'in the GUI; choose demonstration-only passwords. No accounts are preinstalled.\n\n'
        'Use one writer at a time. Close the GUI before using the console in this directory. '
        'To start again, prepare a different new directory and retain this one. '
        'Do not delete maintenance logs or unknown temporary snapshots.\n',
        encoding='utf-8', newline='\n')
    print('Ready. Start LabSlot-GUI.exe in the new directory; register synthetic accounts in the GUI.')
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, help='Absolute, nonexistent directory under an existing parent.')
    parser.add_argument('--confirm-new-directory', action='store_true',
                        help='Explicitly confirm initialization of the exact new --target directory without prompting.')
    args = parser.parse_args()
    try:
        prepare(args.target, args.confirm_new_directory)
    except (OSError, ValueError) as error:
        parser.exit(2, str(error) + '\n')


if __name__ == '__main__':
    main()
