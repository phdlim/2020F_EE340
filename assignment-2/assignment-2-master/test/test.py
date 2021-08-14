#!/usr/bin/python3

import redis
import subprocess
import argparse
import random
import os
import sys
import requests
import random, string
import time
def random_str(length, letter_set=string.ascii_lowercase):
    return ''.join(random.choice(letter_set) for i in range(length))

class Testset:
    def __init__(self, server_port, redis_ip, redis_port):
        self.server_port = server_port
        self.redis = redis.Redis(host=redis_ip, port=redis_port)

    def func_get(self, key_len=16, val_len=32):
        k = random_str(key_len)
        v = random_str(val_len)
        self.redis.set(k, v)
        print("Testing key=%r" % k)
        r = requests.get('http://127.0.0.1:{}/{}'.format(self.server_port, k))

        if not r.ok:
            print("Got status code of {}".format(r.status_code))
            return False

        print("Expected: %r, Got: %r" % (v, r.text))
        return v == r.text

    def func_set(self, cnt=1, key_len=16, val_len=32):
        data = {}
        for i in range(cnt):
            data[random_str(key_len)] = random_str(val_len)

        r = requests.post('http://127.0.0.1:{}/'.format(self.server_port), data=data)

        if not r.ok:
            print("Got status code of {}".format(r.status_code))
            return False

        for key, value in data.items():
            stored = self.redis.get(key)
            vb = value.encode('utf-8')
            print("Expected: %r, Got: %r" % (vb, stored))
            if vb != stored:
                return False
        return True

    def func_set_simple(self):
        return self.func_set(1)

    def func_set_multiple(self):
        return self.func_set(8)

    def robust_get_1(self):
        return self.func_get(key_len=1024, val_len=40960)

    def robust_set_1(self):
        return self.func_set(cnt=8, key_len=1024, val_len=40960)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--test", required=True)
    parser.add_argument("--valgrind", action="store_true", default=False)
    parser.add_argument("--redis-port", type=int, default=6379)
    parser.add_argument("--redis-ip", type=str, default='127.0.0.1')
    parser.add_argument("binary")

    args = parser.parse_args()

    server_port = None
    while True:
        server_port = random.randrange(10000, 30000)
        if os.system("netstat -nltd4 | grep :{}".format(server_port)) != 0:
            break

    if not os.path.exists(args.binary):
        print("File {} does not exist.".format(args.binary), file=sys.stderr)
        sys.exit(1)

    print("Starting server...", file=sys.stderr)

    cmdline = [args.binary, str(server_port), '127.0.0.1', '6379']
    if args.valgrind:
        cmdline = ["valgrind", "-v", "--leak-check=full"] + cmdline
    proc = subprocess.Popen(cmdline, stderr=subprocess.PIPE if args.valgrind else None)

    test = Testset(server_port, args.redis_ip, args.redis_port)
    if not hasattr(test, args.test):
        print("Invalid test {}.".format(args.test), file=sys.stderr)
        sys.exit(1)

    time.sleep(1)
    test_pass = getattr(test, args.test)()

    if test_pass:
        print("Test Passed.", file=sys.stderr)
    else:
        print("Test Failed.", file=sys.stderr)

    print("Terminating server...", file=sys.stderr)
    proc.terminate()
    _, stderr_out = proc.communicate()

    if args.valgrind:
        try:
            stderr_out = stderr_out.decode("utf-8")
        except:
            pass
        print(stderr_out, file=sys.stderr)

        valgrind_ok = True
        for line in stderr_out.split('\n'):
            if 'ERROR SUMMARY: ' in line:
                if '0 errors' not in line:
                    valgrind_ok = False
                    print("Detected memory leak!", file=sys.stderr)
                    break
        sys.exit(0 if valgrind_ok else 1)
    else:
        sys.exit(0 if test_pass else 1)
