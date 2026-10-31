from typing import Literal
from fastapi import APIRouter, Depends, HTTPException, Query, Request
from .common import identity


def report_router(authenticated):
    router = APIRouter(prefix="/api/reports", dependencies=[Depends(authenticated)])

    @router.get("")
    def reports(request: Request, agent: str | None = None, limit: int = Query(default=50, ge=1, le=200),
                scope: Literal["live", "history"] = "history", start_ms: int | None = Query(default=None, ge=0),
                end_ms: int | None = Query(default=None, ge=0), cursor: str | None = Query(default=None, max_length=500)):
        if start_ms is not None and end_ms is not None and start_ms > end_ms:
            raise HTTPException(422, "Start time must not be after end time")
        try:
            return request.app.state.repository.history_page(identity(agent) if agent else None, limit, scope, start_ms, end_ms, cursor)
        except ValueError as error:
            raise HTTPException(422, str(error))

    return router
