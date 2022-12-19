import fcntl
import os
import random
import shutil
import subprocess
from dataclasses import dataclass
from tempfile import NamedTemporaryFile, TemporaryDirectory
from typing import Callable, Dict, List, Optional, Tuple, Type

import pytest

MAJOR = 235
BUF_LEN = 128

random.seed(42)

def detect_msg_slot_channel_num() -> int:
    prog = '#include <stdio.h>\n#include "message_slot.h"\n\nint main(){printf("%d", MSG_SLOT_CHANNEL);}'
    with TemporaryDirectory() as d:
        shutil.copy("message_slot.h", os.path.join(d, "message_slot.h"))
        with open(os.path.join(d, "detector.c"), "w") as f:
            f.write(prog)
        res = subprocess.run(["gcc", "-o", "ioctldetector", f.name])
        res.check_returncode()
        try:
            res = subprocess.run(["./ioctldetector"], stdout=subprocess.PIPE)
            res.check_returncode()
            return int(res.stdout)
        finally:
            os.remove("./ioctldetector")
    
MSG_SLOT_CHANNEL = detect_msg_slot_channel_num()

@dataclass
class ExpectedException:
    exception: Type[Exception]
    msg: str


@dataclass
class Operation:
    exp_exception: Optional[ExpectedException]

    def execute(self):
        pass

    def cleanup(self):
        pass


@dataclass
class CreateSlot(Operation):
    name: str
    minor: int

    def execute(self):
        self.filename = f"/dev/{self.name}"
        os.system(f"sudo mknod {self.filename} c {MAJOR} {self.minor}")
        os.system(f"sudo chmod a+rw {self.filename}")

    def cleanup(self):
        if os.path.exists(self.filename):
            os.system(f"sudo rm {self.filename}")


@dataclass
class DeleteSlot(Operation):
    name: str

    def execute(self):
        filename = f"/dev/{self.name}"
        os.system(f"sudo rm {filename}")


@dataclass
class Read(Operation):
    filename: str
    channel: Optional[int]
    n: int
    expected: bytes
    preprocessor: Callable[[bytes], bytes] = lambda x: x

    def execute(self):
        f = os.open(self.filename, os.O_RDONLY)
        try:
            if self.channel is not None:
                fcntl.ioctl(f, MSG_SLOT_CHANNEL, self.channel)
            msg = os.read(f, self.n)
        finally:
            os.close(f)
        assert self.preprocessor(msg) == self.expected


@dataclass
class Send(Operation):
    filename: str
    channel: Optional[int]
    msg: bytes

    def execute(self):
        f = os.open(self.filename, os.O_WRONLY)
        try:
            if self.channel is not None:
                fcntl.ioctl(f, MSG_SLOT_CHANNEL, self.channel)
            os.write(f, self.msg)
        finally:
            os.close(f)


@dataclass
class CompileSender(Operation):
    def execute(self):
        if os.system("gcc -O3 -Wall -std=c11 message_sender.c -o message_sender") > 0:
            raise Exception("message_sender compilation failed")

    def cleanup(self):
        if os.path.exists("./message_sender"):
            os.remove("./message_sender")


@dataclass
class CompileReader(Operation):
    def execute(self):
        if os.system("gcc -O3 -Wall -std=c11 message_reader.c -o message_reader") > 0:
            raise Exception("message_reader compilation failed")

    def cleanup(self):
        if os.path.exists("./message_reader"):
            os.remove("./message_reader")


@dataclass
class ReaderRead(Read):
    def execute(self):
        res = subprocess.run(
            ["./message_reader", self.filename, f"{self.channel}"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        res.check_returncode()
        print(res)
        assert self.preprocessor(res.stdout) == self.expected


@dataclass
class SenderSend(Send):
    def execute(self):
        res = subprocess.run(
            ["./message_sender", self.filename, f"{self.channel}", self.msg],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        res.check_returncode()


def order_retaining_merge(*lsts: list):
    indices = [0 for _ in range(len(lsts))]
    merged = []
    for _ in range(sum(len(lst) for lst in lsts)):
        i = random.choice([i for i in range(len(indices)) if indices[i] < len(lsts[i])])
        merged.append(lsts[i][indices[i]])
        indices[i] += 1
    return merged


def generate_send_read_ops(n: int) -> List[Operation]:
    ops = []
    for i in range(n):
        seq: List[Operation] = [
            CreateSlot(name=f"message_slot{i}", minor=i, exp_exception=None),
        ]
        seq += order_retaining_merge(
            *[
                [
                    Send(
                        filename=f"/dev/message_slot{i}",
                        channel=j,
                        msg=f"slot {i} channel {j}".encode(),
                        exp_exception=None,
                    ),
                    Read(
                        filename=f"/dev/message_slot{i}",
                        channel=j,
                        n=BUF_LEN,
                        expected=f"slot {i} channel {j}".encode(),
                        preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                        exp_exception=None,
                    ),
                ]
                for j in range(1, n)
            ]
        )
        seq.append(DeleteSlot(name=f"message_slot{i}", exp_exception=None))
        ops.append(seq)
    return order_retaining_merge(*ops)


test_cases = [
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=None),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"hello world",
                exp_exception=None,
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"hello world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=None,
            ),
            DeleteSlot(name="message_slot42", exp_exception=None),
        ],
        id="sanity",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=None),
            CompileSender(exp_exception=None),
            CompileReader(exp_exception=None),
            SenderSend(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"hello world",
                exp_exception=None,
            ),
            ReaderRead(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"hello world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=None,
            ),
            DeleteSlot(name="message_slot42", exp_exception=None),
        ],
        id="use message_sender and message_reader",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=None),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"hello world",
                exp_exception=None,
            ),
            Send(
                filename="/dev/message_slot42",
                channel=99999,
                msg=b"goodbye world",
                exp_exception=None,
            ),
            Read(
                filename="/dev/message_slot42",
                channel=99999,
                n=BUF_LEN,
                expected=b"goodbye world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=None,
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"hello world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=None,
            ),
            DeleteSlot(name="message_slot42", exp_exception=None),
        ],
        id="writing to multiple channels and then reading",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=None),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"hello world",
                exp_exception=None,
            ),
            CreateSlot(name="message_slot45", minor=7, exp_exception=None),
            Send(
                filename="/dev/message_slot45",
                channel=99999,
                msg=b"goodbye world",
                exp_exception=None,
            ),
            Read(
                filename="/dev/message_slot45",
                channel=99999,
                n=BUF_LEN,
                expected=b"goodbye world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=None,
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"hello world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=None,
            ),
            DeleteSlot(name="message_slot42", exp_exception=None),
            DeleteSlot(name="message_slot45", exp_exception=None),
        ],
        id="writing to multiple channels in multiple slots and then reading",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=5, exp_exception=None),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"z" * 500,
                exp_exception=ExpectedException(
                    msg="Message too long", exception=OSError
                ),
            ),
            DeleteSlot(name="message_slot42", exp_exception=None),
        ],
        id="message too long write error",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=5, exp_exception=None),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"lorem ipsum",
                exp_exception=None,
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=2,
                expected=b"lo",
                exp_exception=ExpectedException(
                    msg="No space left on device", exception=OSError
                ),
            ),
            DeleteSlot(name="message_slot42", exp_exception=None),
        ],
        id="read buffer too small error",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot43", minor=6, exp_exception=None),
            Send(
                filename="/dev/message_slot43",
                channel=97,
                msg=b"lorem ipsum",
                exp_exception=None,
            ),
            Read(
                filename="/dev/message_slot43",
                channel=None,
                n=BUF_LEN,
                expected=b"",
                exp_exception=ExpectedException(
                    msg="Invalid argument", exception=OSError
                ),
            ),
            DeleteSlot(name="message_slot43", exp_exception=None),
        ],
        id="reading before setting channel error",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot43", minor=6, exp_exception=None),
            Send(
                filename="/dev/message_slot43",
                channel=None,
                msg=b"lorem ipsum",
                exp_exception=ExpectedException(
                    msg="Invalid argument", exception=OSError
                ),
            ),
            DeleteSlot(name="message_slot43", exp_exception=None),
        ],
        id="writing before setting channel error",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=None),
            Read(
                filename="/dev/message_slot42",
                channel=22,
                n=BUF_LEN,
                expected=b"",
                exp_exception=ExpectedException(
                    msg="Resource temporarily unavailable", exception=OSError
                ),
            ),
            DeleteSlot(name="message_slot42", exp_exception=None),
        ],
        id="reading from empty channel error",
    ),
    pytest.param(
        generate_send_read_ops(128),
        id="stress testing - multi slot multi channel",
    ),
]


def install_device():
    os.system("make")
    os.system("sudo insmod message_slot.ko")
    os.system("make clean")


def uninstall_device():
    os.system("sudo rmmod message_slot")


@pytest.mark.parametrize("operations", test_cases)
def test_message_slot(operations: List[Operation]):
    install_device()
    cleanups: List[Callable] = []
    try:
        for op in operations:
            if op.exp_exception is not None:
                with pytest.raises(
                    op.exp_exception.exception, match=op.exp_exception.msg
                ):
                    op.execute()
                continue
            op.execute()
            cleanups.append(op.cleanup)
    finally:
        try:
            for cleanup in reversed(cleanups):
                cleanup()
        finally:
            uninstall_device()
