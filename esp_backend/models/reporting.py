from pydantic import BaseModel, ConfigDict, Field


class ReportingInterval(BaseModel):
    model_config = ConfigDict(extra="forbid")
    interval_seconds: int = Field(strict=True, ge=1, le=300)
