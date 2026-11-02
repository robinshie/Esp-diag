from fastapi import APIRouter, Depends, HTTPException, Query, Request

from ..ai_provider import ProviderUnavailable
from .common import identity
from esp_protocol.report_limits import MAX_REPORT_LINES


def ai_router(authenticated):
    router = APIRouter(prefix="/api", dependencies=[Depends(authenticated)])

    @router.get("/ai/status")
    def status(request: Request):
        return request.app.state.explanations.status()

    @router.post("/reports/{report_id}/analyze")
    def analyze(request: Request, report_id: str, agent: str,
                line: int | None = Query(default=None, ge=0, le=MAX_REPORT_LINES - 1)):
        report = request.app.state.repository.saved_report(identity(agent), report_id)
        if report is None or (line is not None and line >= len(report["lines"])):
            raise HTTPException(404, "Saved report or log line not found")
        try:
            return request.app.state.explanations.explain(report, line)
        except ProviderUnavailable as error:
            raise HTTPException(503, str(error))

    return router
