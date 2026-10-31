from contextlib import asynccontextmanager
import secrets

from fastapi import FastAPI, Header, HTTPException
from fastapi.responses import JSONResponse
from pymongo.errors import DuplicateKeyError, PyMongoError

from .ingestion import ReportIngestor
from .repository import MongoRepository
from .models import DeviceMetadata, ReportingInterval  # public import compatibility
from .controllers.devices import device_router
from .controllers.reports import report_router


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

    @app.exception_handler(DuplicateKeyError)
    async def duplicate_number(request, exc):
        return JSONResponse({"detail": "设备编号已被其他设备使用"}, status_code=409)

    @app.exception_handler(PyMongoError)
    async def unavailable(request, exc):
        return JSONResponse({"detail": "Database unavailable; live MQTT and pending archive are preserved"}, status_code=503)

    @app.get("/api/health")
    def health():
        app.state.repository.ping()
        intake = app.state.ingestor
        return {"database": "connected", "mqtt_connected": intake.connected.is_set(),
                "pending_reports": intake.pending(), "archive_error": intake.archive_error}

    app.include_router(device_router(settings, authenticated))
    app.include_router(report_router(authenticated))

    return app
