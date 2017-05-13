#!/usr/bin/env python3

from pprint import pprint
from select import select
import importlib
import json
import re
import sys
import telnetlib
import traceback
telnetlib.GMCP = b'\xc9'


def log(*args):
    sys.stdout.write("     -----")
    print(*args)
    pass


class Session(object):
    def __init__(self, world_module):
        self.gmcp = {}
        world_class = world_module.get_class()
        host_port = world_class.get_host_port()
        self.telnet = self.connect(*host_port)
        self.world_module = world_module
        self.world = world_class(self)
        self.gmcp = {}

    def join(self):
        self.thr.join()

    def log(*args):
        global log
        log(*args[1:])

    def strip_ansi(self, line):
        return re.sub(r'(\x9B|\x1B\[)[0-?]*[ -\/]*[@-~]', '', line)

    def iac(self, sock, cmd, option):
        if cmd == telnetlib.WILL:
            if option == telnetlib.GMCP:
                log("Enabling GMCP")
                sock.sendall(telnetlib.IAC + telnetlib.DO + option)
            else:
                sock.sendall(telnetlib.IAC + telnetlib.DONT + option)
        elif cmd == telnetlib.SE:
            data = self.telnet.read_sb_data()
            if data[0] == ord(telnetlib.GMCP):
                self.handleGMCP(data[1:].decode('utf-8'))

    def handleGMCP(self, data):
        # this.that {JSON blob}
        space_idx = data.find(' ')
        whole_key = data[:space_idx]
        nesting = whole_key.split('.')
        current = self.gmcp
        for nest in nesting[:-1]:
            current[nest] = {}
            current = current[nest]
        current[nesting[-1]] = json.loads(data[space_idx + 1:])
        self.world.gmcp(whole_key)

    def connect(self, host, port):
        t = telnetlib.Telnet()
        t.set_option_negotiation_callback(self.iac)
        # t.set_debuglevel(1)
        t.open(host, int(port))
        return t

    def send(self, line):
        self.telnet.write((line + '\n').encode('utf-8'))

    def handle_input(self):
        data = self.telnet.read_very_eager().decode('utf-8')
        if not data:
            _ = self.telnet.read_sb_data()
        for line in data.strip().split('\n'):
            replacement = None
            try:
                replacement = self.world.trigger(line.strip())
            except Exception as e:
                traceback.print_exc()
            if replacement is not None:
                line = replacement
            print(line)


    def handle_output(self):
        data = input()
        if data == '#reload' and self.world:
            log('Reloading world')
            try:
                self.world_module = importlib.reload(self.world_module)
                self.world = self.world_module.get_class()(self)
            except Exception:
                traceback.print_exc()
            return
        else:
            handled = False
            try:
                handled = self.world.alias(data)
            except Exception as e:
                traceback.print_exc()
            else:
                if not handled:
                    self.send(data)


    def run(self):
        try:
            while True:
                try:
                    fds, _, _ = select([self.telnet.get_socket(), sys.stdin], [], [])
                except KeyboardInterrupt:
                    break
                for fd in fds:
                    if fd == self.telnet.get_socket():
                        self.handle_input()
                    elif fd == sys.stdin:
                        self.handle_output()
        finally:
            self.telnet.close()


def main():
    if len(sys.argv) != 2:
        print("Usage: {} worldmodule (without .py)".format(sys.argv[0]))
        exit(1)

    world_module = importlib.import_module(sys.argv[1])
    ses = Session(world_module)
    ses.run()


assert(__name__ == '__main__')
main()