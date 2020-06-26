#!/usr/bin/env python3

""" Clean up stale pods in the testing cluster.

    When pushing a new commit before the current CI finishes, CI is
    immediatly interrupted. We don't a chance to clean up for this case, so
    this script will be used to clean up staled pods in the testing cluster.
    We assume `kubectl` is correctly configured with the credentials.
"""

import subprocess
import json
import datetime
from typing import List, Dict

# We can make this configurable if needed
MAX_DAYS = 3


def get_pods() -> List[str]:
    """ Get a list of pods for names
    """
    out = subprocess.check_output([
        "kubectl",
        "get",
        "pods",
        "-o", "name",
    ]).splitlines()
    out_str = map(lambda x: str(x, 'utf-8'), out)
    out_no_kontaind = filter(
        lambda x: not x.startswith("pod/kontaind"), out_str)
    out_no_prefix = map(lambda x: x.replace("pod/", ""), out_no_kontaind)
    return list(out_no_prefix)


def get_pod_create_time(pod) -> datetime.datetime:
    """ Get the pods starting time
    """
    out = subprocess.check_output([
        "kubectl",
        "get",
        "pods",
        pod,
        "-o",
        "json",
    ])
    out_json = json.loads(out)
    return datetime.datetime.strptime(out_json["status"]["startTime"], "%Y-%m-%dT%H:%M:%SZ")


def delete_pods(pods: List[str]):
    """ Delete the pods through kubectl
    """
    for pod in pods:
        subprocess.run([
            "kubectl",
            "delete",
            "pods",
            pod,
        ])


def get_stale_pods(pods_date_map: Dict[str, datetime.datetime]) -> List[str]:
    """ Get a list of staled pods that started before 'today' - MAX_DAYS
    """
    today = datetime.datetime.today()
    staled_pods = filter(lambda elem: (
        (elem[1] + datetime.timedelta(days=MAX_DAYS)) < today), pods_date_map.items())
    staled_pods_no_date = map(lambda elem: elem[0], staled_pods)
    return list(staled_pods_no_date)


def main():
    print(f"Cleaning stale tests older than {MAX_DAYS} days")
    pods: List[str] = get_pods()
    pods_date_map: Dict[str, datetime.datetime] = dict(
        (pod, get_pod_create_time(pod)) for pod in pods)
    staled_pods: Dict[str, datetime.datetime] = get_stale_pods(pods_date_map)
    delete_pods(staled_pods)


if __name__ == "__main__":
    main()
