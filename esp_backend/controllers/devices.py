from typing import Literal
from fastapi import APIRouter, Depends, HTTPException, Request
from ..models import DeviceMetadata, ReportingInterval
from .common import identity
from esp_gateway.reporting_config import ReportingConfig


def device_router(settings, authenticated):
    router = APIRouter(prefix="/api/devices", dependencies=[Depends(authenticated)])

    def reporting(device_id, interval_ms=None):
        path = getattr(settings, "config_path", None)
        if not path:
            raise HTTPException(503, "Reporting configuration unavailable")
        try:
            config = ReportingConfig(path)
            return config.read(device_id) if interval_ms is None else config.save(device_id, interval_ms)
        except KeyError:
            raise HTTPException(404, "This gateway does not manage that ESP")
        except (OSError, ValueError):
            raise HTTPException(503, "Cannot read or save reporting configuration")

    @router.get("")
    def devices(request: Request, scope: Literal["live", "history"] = "history"):
        return {"devices": request.app.state.repository.list_devices(scope), "history_days": settings.history_days, "scope": scope}

    @router.put("/{device_id}")
    def save(request: Request, device_id: str, metadata: DeviceMetadata):
        return request.app.state.repository.save_device(identity(device_id), metadata.model_dump(exclude_none=True))

    @router.delete("/{device_id}")
    def delete(request: Request, device_id: str):
        request.app.state.repository.delete_device(identity(device_id))
        return {"deleted": True, "history_retained": True}

    @router.get("/{device_id}/reporting")
    def get_reporting(device_id: str):
        return reporting(identity(device_id))

    @router.put("/{device_id}/reporting")
    def save_reporting(device_id: str, value: ReportingInterval):
        return reporting(identity(device_id), value.interval_seconds * 1000)

    return router
