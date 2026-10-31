from contextlib import asynccontextmanager
import secrets
from typing import Literal

from fastapi import Depends, FastAPI, Header, HTTPException, Query
from fastapi.responses import JSONResponse
from pydantic import BaseModel, ConfigDict, Field
from pymongo.errors import PyMongoError

from .ingestion import ReportIngestor
from .repository import MongoRepository, agent_id


class DeviceMetadata(BaseModel):
    model_config = ConfigDict(extra="forbid")
    name: str = Field(min_length=1, max_length=128)
    location: str = Field(default="未设置", max_length=256)
    enabled: bool = True


def create_app(settings, repository=None, ingestor=None):
    @asynccontextmanager
    async def lifespan(app):
        repo = repository or MongoRepository(settings)
        repo.initialize()
        intake = ingestor or ReportIngestor(settings, repo)
        app.state.repository, app.state.ingestor = repo, intake
        intake.start()
        try:
            yield
        finally:
            intake.stop()
            repo.close()

    app = FastAPI(title="ESP32 registry and archive", lifespan=lifespan, docs_url=None, redoc_url=None, openapi_url=None)

    def authenticated(authorization: str | None = Header(default=None)):
        if not authorization or not secrets.compare_digest(authorization, "Bearer " + settings.token):
            raise HTTPException(401, "API authentication required")

    def identity(value):
        try:
            return agent_id(value)
        except ValueError:
            raise HTTPException(422, "Invalid ESP32 ID")

    @app.exception_handler(PyMongoError)
    async def unavailable(request, exc):
        return JSONResponse({"detail": "Database unavailable; live MQTT and pending archive are preserved"}, status_code=503)

    @app.get("/api/health")
    def health():
        app.state.repository.ping()
        intake = app.state.ingestor
        return {"database": "connected", "mqtt_connected": intake.connected.is_set(),
                "pending_reports": intake.pending(), "archive_error": intake.archive_error}

    @app.get("/api/devices", dependencies=[Depends(authenticated)])
    def devices(scope: Literal["live", "history"] = "history"):
        return {"devices": app.state.repository.list_devices(scope), "history_days": settings.history_days, "scope": scope}

    @app.put("/api/devices/{device_id}", dependencies=[Depends(authenticated)])
    def save(device_id: str, metadata: DeviceMetadata):
        return app.state.repository.save_device(identity(device_id), metadata.model_dump())

    @app.delete("/api/devices/{device_id}", dependencies=[Depends(authenticated)])
    def delete(device_id: str):
        app.state.repository.delete_device(identity(device_id))
        return {"deleted": True, "history_retained": True}

    @app.get("/api/reports", dependencies=[Depends(authenticated)])
    def reports(agent: str | None = None, limit: int = Query(default=50, ge=1, le=200),
                scope: Literal["live", "history"] = "history", start_ms: int | None = Query(default=None, ge=0),
                end_ms: int | None = Query(default=None, ge=0), cursor: str | None = Query(default=None, max_length=500)):
        if start_ms is not None and end_ms is not None and start_ms > end_ms:
            raise HTTPException(422, "Start time must not be after end time")
        try:
            return app.state.repository.history_page(identity(agent) if agent else None, limit, scope, start_ms, end_ms, cursor)
        except ValueError as error:
            raise HTTPException(422, str(error))

    return app
