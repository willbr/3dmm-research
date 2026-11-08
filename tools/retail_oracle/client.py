"""Control the hash-pinned, injected 32-bit retail test bridge over a local pipe."""
import argparse
import json
import pathlib
import struct
import time

MAGIC = 0x314f5233
EXE_SHA256 = 'cc7c05cd4b63c0d1f2a0cee6fed86d85bd9f54e35952818f412f128425b2bca5'
V3DMM_SHA256 = 'bd1b7e6d81c23bfec5050887617e6cb4e85b375aa762c599b57fe0bec36af39f'


class OracleError(RuntimeError):
    pass


class RetailOracle:
    def __init__(self, pid, timeout=20, expected_hash=EXE_SHA256):
        self.pid, self.sequence = pid, 0
        pipe = r'\\.\pipe\3dmm-retail-oracle-' + str(pid)
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.stream = open(pipe, 'r+b', buffering=0)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise OracleError('Retail pipe did not become available')
                time.sleep(.1)
        self.identity = self.request(0)
        if self.identity['pid'] != pid or self.identity['exe_sha256'] != expected_hash:
            self.close()
            raise OracleError('Retail bridge identity mismatch')

    def request(self, operation, *arguments, path=''):
        self.sequence += 1
        if len(arguments) > 8:
            raise ValueError('Too many bridge arguments')
        encoded = str(path).encode('utf-16-le')
        if len(encoded) >= 1024:
            raise ValueError('Bridge path is too long')
        packet = struct.pack('<4I8i', MAGIC, 1, operation, self.sequence,
                             *(list(arguments) + [0] * (8 - len(arguments)))) + encoded.ljust(1024, b'\0')
        assert len(packet) == 1072
        self.stream.write(packet)

        def read(count):
            result = bytearray()
            while len(result) < count:
                block = self.stream.read(count - len(result))
                if not block:
                    raise OracleError('Retail pipe closed')
                result.extend(block)
            return bytes(result)

        size, = struct.unpack('<I', read(4))
        if size > 16 * 1024 * 1024:
            raise OracleError('Invalid bridge response length')
        response = json.loads(read(size))
        if not response['ok']:
            raise OracleError(response['error'])
        return response['result']

    def hello(self):
        return self.request(0)

    def state(self):
        return self.request(1)

    def seek(self, scene, frame):
        """Use one-based Studio UI coordinates, not stored scene frame labels."""
        return self.request(2, scene, frame)

    def command(self, cid, *arguments):
        return self.request(3, cid, *arguments)

    def capture(self, destination):
        return self.request(4, path=pathlib.Path(destination).resolve())

    def close(self):
        self.stream.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('action', choices=('hello', 'state', 'seek', 'capture', 'command'))
    parser.add_argument('arguments', nargs='*')
    args = parser.parse_args()
    oracle = RetailOracle(args.pid)
    try:
        if args.action in ('hello', 'state'):
            result = getattr(oracle, args.action)()
        elif args.action == 'capture':
            result = oracle.capture(args.arguments[0])
        else:
            result = getattr(oracle, args.action)(*[int(value, 0) for value in args.arguments])
        print(json.dumps(result, indent=2))
    finally:
        oracle.close()


if __name__ == '__main__':
    main()
