"""The local test bridge's database, for tools/runpublic.ps1 -ranked
(EMBER_LIBRARY.md section 28). Only ever a bridge this PC's tools started in
a scratch folder: the path must be a file named bridge.sqlite3 under a folder
named bridge.

  bridgedb.py seed <db> <ember id 0> <ember id 1>
      Gives both Ember IDs a Discord account, as Discord's sign-in would
      (players player1 and player2), and the first one a settled rating in
      SFV's ranking (band 8) and in USF4's (band 6), last played now. Waits
      up to 30 s for the bridge to know both Ember IDs.
  bridgedb.py rows <db> <sql>
      Prints the rows of a read-only query as JSON.
"""
import json
import sqlite3
import sys
import time
from pathlib import Path

ACCOUNTS = ("900000000000000001", "900000000000000002")
# A rating in the middle of a band (1050 + 100 * band), settled (deviation 60).
SFV_BAND, USF4_BAND = 8, 6


def connect(path: str) -> sqlite3.Connection:
    db = Path(path)
    if db.name != "bridge.sqlite3" or db.parent.name != "bridge" or not db.is_file():
        raise SystemExit(f"not a test bridge's database: {path}")
    connection = sqlite3.connect(db, timeout=10)
    connection.execute("PRAGMA busy_timeout = 10000")
    return connection


def seed(path: str, first: str, second: str) -> None:
    connection = connect(path)
    deadline = time.time() + 30
    while True:
        known = connection.execute(
            "SELECT COUNT(*) FROM identities WHERE ember_id IN (?, ?)", (first, second)).fetchone()[0]
        if known == 2:
            break
        if time.time() > deadline:
            raise SystemExit(f"the bridge knows {known} of the two Ember IDs")
        time.sleep(1)
    now = int(time.time())
    with connection:
        for account, ember_id, name in zip(ACCOUNTS, (first, second), ("player1", "player2")):
            connection.execute(
                "INSERT INTO discord_accounts (user_id, ember_id, username, connected_at) VALUES (?, ?, ?, ?)",
                (account, ember_id, name, now))
        for game, band in (("sfv", SFV_BAND), ("usf4", USF4_BAND)):
            connection.execute(
                "INSERT INTO skill_ratings (game, discord_user_id, rating, deviation, volatility, band, played_at)"
                " VALUES (?, ?, ?, 60, 0.06, 0, ?)",
                (game, ACCOUNTS[0], 1050.0 + 100.0 * band, now))
    print(json.dumps({"accounts": list(ACCOUNTS), "sfv_band": SFV_BAND, "usf4_band": USF4_BAND, "at": now}))


def rows(path: str, sql: str) -> None:
    if not sql.lstrip().upper().startswith("SELECT"):
        raise SystemExit("rows takes a SELECT")
    connection = connect(path)
    connection.execute("PRAGMA query_only = ON")
    cursor = connection.execute(sql)
    names = [column[0] for column in cursor.description]
    print(json.dumps([dict(zip(names, row)) for row in cursor.fetchall()]))


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "seed":
        seed(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) == 4 and sys.argv[1] == "rows":
        rows(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)
