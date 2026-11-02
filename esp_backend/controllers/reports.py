from typing import Literal
from fastapi import APIRouter, Depends, HTTPException, Query, Request
from .common import identity
from esp_backend.log_ai import predict
from esp_protocol.report_limits import MAX_REPORT_LINES


def report_router(authenticated):
    router = APIRouter(prefix="/api/reports", dependencies=[Depends(authenticated)])

    @router.get('/{report_id}/context')
    def context(request: Request, report_id: str, agent: str):
        result = request.app.state.repository.report_context(identity(agent), report_id)
        if result is None:
            raise HTTPException(404, 'Saved report not found')
        return result

    @router.get("/{report_id}/explain")
    def explain(request: Request, report_id: str, agent: str, line: int = Query(ge=0, le=MAX_REPORT_LINES - 1)):
        report = request.app.state.repository.saved_report(identity(agent), report_id)
        if not report or line >= len(report['lines']):
            raise HTTPException(404, "Saved report or log line not found")
        try:
            prediction = predict(report['lines'][line]['text'])
        except (OSError, ValueError, KeyError):
            raise HTTPException(503, "Log explanation model unavailable")
        return {**prediction, 'report_id': report_id, 'line': line, 'source': 'database',
                'execution': 'backend', 'purpose': 'advisory', 'device_control': False,
                'evidence_text': report['lines'][line]['text']}

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
