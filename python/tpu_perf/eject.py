#!/usr/bin/env python3

import os

def main():
    import argparse
    parser = argparse.ArgumentParser(description='Eject dir in model zoo')
    parser.add_argument(
        '--extra', '-e', type=str,
        help='Extra file list', required=True)
    parser.add_argument(
        '--out', '-O', type=str,
        help='Output tar file', required=True)
    parser.add_argument(
        'dir', metavar='DIR', type=str,
        help='Directory to eject')
    args = parser.parse_args()

    with open(args.extra) as f:
        extra_files = [line.strip(' \n') for line in f.readlines()]
        extra_files = [line for line in extra_files if line]

    import tarfile
    with tarfile.open(args.out, 'w:bz2') as tar:
        for fn in extra_files:
            tar.add(
                fn, recursive=False,
                arcname=os.path.join(f'model-zoo-{args.dir}', fn))
        for fn in os.listdir(args.dir):
            tar.add(
                os.path.join(args.dir, fn),
                arcname=os.path.join(f'model-zoo-{args.dir}', fn))

if __name__ == '__main__':
    main()
