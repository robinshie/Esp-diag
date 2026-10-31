from fastapi import HTTPException
from ..repository import agent_id


def identity(value):
    try:
        return agent_id(value)
    except ValueError:
        raise HTTPException(422, "Invalid ESP32 ID")
