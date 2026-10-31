from pydantic import BaseModel, ConfigDict, Field


class DeviceMetadata(BaseModel):
    model_config = ConfigDict(extra="forbid")
    name: str = Field(min_length=1, max_length=128)
    location: str = Field(default="未设置", max_length=256)
    enabled: bool = True
    # Omission preserves an existing number for older API clients; "" clears it.
    device_number: str | None = Field(default=None, max_length=32, pattern=r"^[A-Za-z0-9._-]*$")
