"""Resolve a configured USB identity on every reconnect, without guessing a board."""
import serial
from serial.tools import list_ports


def resolve_port(configured_port, serial_number=None):
    if serial_number is None:
        return configured_port
    matches = sorted({port.device for port in list_ports.comports()
                      if port.serial_number == serial_number})
    if len(matches) != 1:
        # Never fall back to a path which might now belong to another board.
        raise serial.SerialException("Configured USB device is absent or ambiguous")
    return matches[0]
