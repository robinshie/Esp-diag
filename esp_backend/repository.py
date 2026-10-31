from datetime import datetime, timezone, timedelta
import hashlib
import re
import base64
import json

from pymongo import ASCENDING, DESCENDING, MongoClient


def agent_id(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", value) or value in {"server", "switch", "service"}:
        raise ValueError("invalid ESP32 ID")
    return value


def validate_report(value):
    if not isinstance(value, dict) or value.get("schema_version") != 1:
        raise ValueError("invalid report schema")
    if not isinstance(value.get("report_id"), str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", value["report_id"]):
        raise ValueError("invalid report identity")
    timestamp = value.get("sent_at_ms")
    if type(timestamp) is not int or timestamp < 0:
        raise ValueError("invalid report timestamp")
    observations, lines = value.get("observations"), value.get("lines")
    if not isinstance(observations, list) or len(observations) > 32 or not isinstance(lines, list) or len(lines) > 64:
        raise ValueError("report exceeds firmware capacity")
    if not all(isinstance(item, dict) and isinstance(item.get("device"), str) for item in observations):
        raise ValueError("invalid observations")
    if not all(isinstance(item, dict) and isinstance(item.get("text"), str) and len(item["text"]) <= 9000
               and type(item.get("at_ms")) is int for item in lines):
        raise ValueError("invalid USB lines")
    if value.get("status") is not None and not isinstance(value["status"], dict):
        raise ValueError("invalid ESP32 runtime status")
    return value


def public_device(row):
    return {"id": row["_id"], "name": row.get("name", row["_id"]), "location": row.get("location", "未设置"),
            "enabled": row.get("enabled", True), "kind": "agent", "manuallyAdded": row.get("manuallyAdded", False),
            "metadata_custom": row.get("metadata_custom", False), "availability": row.get("availability"),
            "availability_at_ms": row.get("availability_at_ms", 0), "latest_report": row.get("latest_report"),
            "report_count": row.get("report_count", 0)}


def connection_session(report):
    status = report.get("status") or {}
    if status.get("reasoning_cycle") != "ACTIVE":
        return None
    parts = []
    for label, value in (("usb", status.get("connection_id")), ("boot", report.get("boot"))):
        if isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", value):
            parts.append(label + ":" + value)
    return "|".join(parts) or None


class MongoRepository:
    def __init__(self, settings):
        options = dict(serverSelectionTimeoutMS=3000, connectTimeoutMS=3000, socketTimeoutMS=5000, appname="esp-diag-backend")
        self.client = (MongoClient(settings.mongo_uri, **options) if settings.mongo_uri else
                       MongoClient(settings.mongo_host, settings.mongo_port, username=settings.username,
                                   password=settings.password, authSource=settings.database, **options))
        self.db = self.client[settings.database]
        self.root = settings.config["agent"]["topic_root"]
        self.devices = self.db.devices
        self.reports = self.db.reports
        self.registrations = self.db.agent_registrations
        self.history_days = settings.history_days

    def initialize(self):
        self.ping()
        self.devices.create_index([("topic_root", ASCENDING)])
        self.reports.create_index([("topic_root", ASCENDING), ("agent_id", ASCENDING),
                                   ("sent_at_ms", DESCENDING), ("_id", DESCENDING)])
        self.reports.create_index([("topic_root", ASCENDING), ("sent_at_ms", DESCENDING), ("_id", DESCENDING)])
        self.reports.create_index([("topic_root", ASCENDING), ("agent_id", ASCENDING),
                                   ("report.status.connection_id", ASCENDING), ("report.boot", ASCENDING),
                                   ("sent_at_ms", DESCENDING), ("_id", DESCENDING)])
        self.reports.create_index("expires_at", expireAfterSeconds=0, name="history_retention")
        if not self.history_days:
            # Remove old TTL deadlines when long-term retention is selected.
            self.reports.update_many({"topic_root": self.root, "expires_at": {"$exists": True}},
                                     {"$unset": {"expires_at": ""}})
        # Remember existing identities before accepting MQTT. A missing device
        # document for a known identity means it was removed, including direct
        # deletes through Mongo Express. History remains independent of inventory.
        identities = set(self.devices.distinct("agent_id", {"topic_root": self.root}))
        identities.update(self.reports.distinct("agent_id", {"topic_root": self.root}))
        for identity in identities:
            self.remember_agent(identity)
            latest = self.reports.find_one({"topic_root": self.root, "agent_id": identity},
                                          sort=[("sent_at_ms", DESCENDING), ("_id", DESCENDING)])
            if latest:
                self.registrations.update_one({"_id": self.key(identity), "last_session": {"$exists": False}},
                    {"$set": {"last_session": connection_session(latest["report"]),
                              "last_session_at_ms": latest["sent_at_ms"]}})

    def remember_agent(self, identity):
        self.registrations.update_one({"_id": self.key(identity)}, {"$setOnInsert": {
            "topic_root": self.root, "agent_id": identity, "first_seen_at": datetime.now(timezone.utc)}}, upsert=True)

    def ping(self):
        self.client.admin.command("ping")

    def key(self, identity):
        return self.root + "/" + agent_id(identity)

    def register_report(self, identity, report, retained=False, name=None):
        identity = agent_id(identity)
        validate_report(report)
        now = datetime.now(timezone.utc)
        key = self.key(identity)
        registration = self.registrations.find_one({"_id": key})
        session = connection_session(report)
        reconnected = (registration is not None and session and registration.get("last_session")
                       and session != registration["last_session"] and not retained
                       and report["sent_at_ms"] > registration.get("last_session_at_ms", 0))
        report_key = hashlib.sha256((key + "/" + report["report_id"]).encode()).hexdigest()
        # Idempotent across MQTT redelivery and a crash between insert and inbox ACK.
        archived = {
            "topic_root": self.root, "agent_id": identity, "sent_at_ms": report["sent_at_ms"],
            "received_at": now, "retained": retained, "report": report}
        if self.history_days:
            archived["expires_at"] = now + timedelta(days=self.history_days)
        self.reports.update_one({"_id": report_key}, {"$setOnInsert": archived}, upsert=True)
        if registration is None or reconnected:
            self.devices.update_one({"_id": key}, {"$setOnInsert": {
                "topic_root": self.root, "agent_id": identity, "name": name or f"ESP32 · {identity}", "location": "未设置",
                "enabled": True, "manuallyAdded": False, "metadata_custom": False, "created_at": now}}, upsert=True)
            self.remember_agent(identity)
        # Retained snapshots and late reports cannot create a new connection or
        # advance discovery memory ahead of the next live USB status report.
        if session and not retained:
            self.registrations.update_one({"_id": key, "$or": [
                {"last_session_at_ms": {"$lt": report["sent_at_ms"]}},
                {"last_session_at_ms": {"$exists": False}}]},
                {"$set": {"last_session": session, "last_session_at_ms": report["sent_at_ms"]}})
        self.devices.update_one({"_id": key, "$or": [{"snapshot_at_ms": {"$lte": report["sent_at_ms"]}},
                                                      {"snapshot_at_ms": {"$exists": False}}]},
                                {"$set": {"latest_report": report, "snapshot_at_ms": report["sent_at_ms"], "updated_at": now}})

    def record_availability(self, identity, state, at_ms):
        if state not in {"ONLINE", "OFFLINE"}:
            raise ValueError("invalid availability")
        self.devices.update_one({"_id": self.key(identity), "$or": [{"availability_at_ms": {"$lte": at_ms}},
                                                                 {"availability_at_ms": {"$exists": False}}]},
                                {"$set": {"availability": state, "availability_at_ms": at_ms}})

    def live_query(self, identity=None):
        rows = self.devices.find({"topic_root": self.root, **({"agent_id": identity} if identity else {})},
                                 {"agent_id": 1})
        sessions = []
        for row in rows:
            registration = self.registrations.find_one({"_id": self.key(row["agent_id"])}) or {}
            session = registration.get("last_session")
            if not session:
                continue
            query = {"agent_id": row["agent_id"]}
            for part in session.split("|"):
                kind, value = part.split(":", 1)
                query["report.status.connection_id" if kind == "usb" else "report.boot"] = value
            sessions.append(query)
        return {"topic_root": self.root, "$or": sessions} if sessions else {"topic_root": self.root, "_id": {"$in": []}}

    def list_devices(self, scope="history"):
        # Count the retained archive itself, including late reports, rather than
        # browser receipts or a counter which could drift after a crash/TTL expiry.
        archives = {row["_id"]: row for row in self.reports.aggregate([
            {"$match": self.live_query() if scope == "live" else {"topic_root": self.root}},
            {"$sort": {"agent_id": 1, "sent_at_ms": -1, "_id": -1}},
            {"$group": {"_id": "$agent_id", "count": {"$sum": 1}, "latest_report": {"$first": "$report"}}}])}
        rows = self.devices.find({"topic_root": self.root}).sort("agent_id", ASCENDING)
        # The cached device snapshot may refer to a report removed in MongoDB's
        # manager or by TTL. Read the latest surviving archive record instead.
        return [public_device({**row, "_id": row["agent_id"],
                               "report_count": archives.get(row["agent_id"], {}).get("count", 0),
                               "latest_report": archives.get(row["agent_id"], {}).get("latest_report")}) for row in rows]

    def save_device(self, identity, metadata):
        identity = agent_id(identity)
        self.remember_agent(identity)
        now = datetime.now(timezone.utc)
        self.devices.update_one({"_id": self.key(identity)}, {"$set": {**metadata, "metadata_custom": True, "updated_at": now},
            "$setOnInsert": {"topic_root": self.root, "agent_id": identity, "created_at": now, "manuallyAdded": True}}, upsert=True)
        count = self.reports.count_documents({"topic_root": self.root, "agent_id": identity})
        return public_device({**self.devices.find_one({"_id": self.key(identity)}), "_id": identity, "report_count": count})

    def delete_device(self, identity):
        # Keep discovery memory so later reports cannot undo the user's removal.
        self.remember_agent(identity)
        self.devices.delete_one({"_id": self.key(identity)})

    def history_page(self, identity=None, limit=50, scope="history", start_ms=None, end_ms=None, cursor=None):
        query = self.live_query(identity) if scope == "live" else {"topic_root": self.root}
        if identity:
            query["agent_id"] = agent_id(identity)
        if start_ms is not None or end_ms is not None:
            query["sent_at_ms"] = {**({"$gte": start_ms} if start_ms is not None else {}),
                                   **({"$lte": end_ms} if end_ms is not None else {})}
        total = self.reports.count_documents(query)
        if cursor:
            try:
                at, key = json.loads(base64.b64decode(cursor + "=" * (-len(cursor) % 4), altchars=b"-_", validate=True))
                if type(at) is not int or at < 0 or not isinstance(key, str) or not re.fullmatch(r"[0-9a-f]{64}", key):
                    raise ValueError()
            except Exception as error:
                raise ValueError("Invalid history cursor") from error
            query = {"$and": [query, {"$or": [{"sent_at_ms": {"$lt": at}}, {"sent_at_ms": at, "_id": {"$lt": key}}]}]}
        rows = list(self.reports.find(query).sort([("sent_at_ms", DESCENDING), ("_id", DESCENDING)]).limit(limit + 1))
        next_cursor = None
        if len(rows) > limit:
            last = rows[limit - 1]
            next_cursor = base64.urlsafe_b64encode(json.dumps([last["sent_at_ms"], last["_id"]]).encode()).decode().rstrip("=")
        return {"reports": [{"agent_id": row["agent_id"], "received_at": row["received_at"].isoformat(),
                             "retained": row["retained"], "report": row["report"]} for row in rows[:limit]],
                "total": total, "next_cursor": next_cursor, "scope": scope, "history_days": self.history_days}

    def history(self, identity=None, limit=50):
        return self.history_page(identity, limit)["reports"]

    def close(self):
        self.client.close()
