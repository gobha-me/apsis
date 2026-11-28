"""Summarize rescue and debt durations from the pilot wide experiment."""

import json
import math
from pathlib import Path
import statistics


STYLES = ("hauler", "crisis", "pathfinder")
LAW_STYLES = ("careful", "bold", "reckless", "payer", "forgetful", "double agent")
TANKS = (3, 4, 6)
DAYS_PER_YEAR = 360


def career_group(careers, style, tank):
    """Select a style and a single tank or the pooled tanks."""
    return [
        career
        for career in careers
        if career["style"] == style and (tank == "all" or career["tank"] == tank)
    ]


def debt_durations(careers, *, minimum_open_days=0):
    """Return completed durations and open-age lower bounds separately."""
    cleared = [days for career in careers for days in career["debtClear"]]
    opened = [
        career["debtOpen"]
        for career in careers
        if career["debtOpen"] >= minimum_open_days
    ]
    return cleared, opened


def quantiles(days):
    """Report the median and nearest-rank p90 in 360-day years."""
    years = sorted(day / DAYS_PER_YEAR for day in days)
    return {
        "count": len(years),
        "p50Years": statistics.median(years) if years else None,
        "p90Years": years[math.ceil(0.9 * len(years)) - 1] if years else None,
    }


def main():
    data = json.loads(Path("pilot_wide.json").read_text(encoding="utf-8"))
    careers = data["careers"]
    output = {}
    print("careers", len(careers), "pilot-years", data["pilotYears"])
    print(
        "%-11s %-5s %7s %7s %6s %6s %6s %6s %6s %5s"
        % (
            "style",
            "tank",
            "rescues",
            "debts",
            "med",
            "p90",
            "max",
            "open*",
            "stuck",
            "flag",
        )
    )
    print(
        "(* debts still open at the end of the 40 years; they are in the "
        "distribution with their age so far, a lower bound)"
    )
    for style in (*STYLES, *LAW_STYLES):
        for tank in (*TANKS, "all") if style in STYLES else (6,):
            group = career_group(careers, style, tank)
            # Keep the historical wide report's count for zero-day ages.
            cleared, opened = debt_durations(group, minimum_open_days=1)
            days = cleared + opened
            stats = quantiles(days)
            row = {
                "careers": len(group),
                "rescues": sum(len(career["rescue"]) for career in group),
                "debts": stats["count"],
                "med": round(stats["p50Years"], 2) if days else None,
                "p90": round(stats["p90Years"], 2) if days else None,
                "max": round(max(days) / DAYS_PER_YEAR, 2) if days else None,
                "openAtEnd": len(opened),
                "stuck": sum(1 for career in group if career["stuck"]),
                "ledger": all(career["ledger"] for career in group),
            }
            output[f"{style}/{tank}"] = row
            print(
                "%-11s %-5s %7d %7d %6s %6s %6s %6d %6d %5s"
                % (
                    style,
                    tank,
                    row["rescues"],
                    row["debts"],
                    row["med"],
                    row["p90"],
                    row["max"],
                    row["openAtEnd"],
                    row["stuck"],
                    "FLAG" if days and row["p90"] > 3 else "",
                )
            )
    Path("agg_wide.json").write_text(json.dumps(output, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
