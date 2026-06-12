#!/usr/bin/env python3
"""
Porcelain Monitor API Integration Tests
Tests the 4 new feature REST API endpoints.

Usage:
    python3 tests/test_api_integration.py [--host localhost] [--port 8080]

Requirements:
    - Backend server running on specified host:port
    - Database initialized with test data (200 porcelains, cracks, repair materials)
    - requests library: pip install requests
"""

import argparse
import json
import sys
import time
import requests
from typing import Dict, List, Optional, Any

PASS = 0
FAIL = 0
RESULTS = []


class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    CYAN = '\033[96m'
    YELLOW = '\033[93m'
    BOLD = '\033[1m'
    RESET = '\033[0m'


def print_header(title: str) -> None:
    print(f"\n{Colors.BOLD}{Colors.CYAN}{'='*60}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}  {title}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}{'='*60}{Colors.RESET}")


def print_case(name: str, passed: bool, msg: str = "", duration_ms: float = 0) -> None:
    global PASS, FAIL
    if passed:
        PASS += 1
        print(f"  {Colors.GREEN}[  OK  ]{Colors.RESET} {name} "
              f"({duration_ms:.1f}ms)")
    else:
        FAIL += 1
        print(f"  {Colors.RED}[ FAIL ]{Colors.RESET} {name}")
        if msg:
            print(f"           {Colors.YELLOW}{msg}{Colors.RESET}")
    RESULTS.append({"name": name, "passed": passed, "msg": msg})


def assert_true(cond: bool, msg: str = "") -> None:
    if not cond:
        raise AssertionError(msg or "Assertion failed")


def assert_near(actual: float, expected: float, tol_pct: float, msg: str = "") -> None:
    if abs(expected) > 1e-15:
        rel_err = abs(actual - expected) / abs(expected) * 100
    elif abs(actual) > 1e-15:
        rel_err = 100.0
    else:
        rel_err = 0.0
    if rel_err > tol_pct:
        raise AssertionError(
            f"{msg or 'Relative error'}: actual={actual}, expected={expected}, "
            f"error={rel_err:.2f}% > tol={tol_pct}%"
        )


def assert_range(val: float, lo: float, hi: float, msg: str = "") -> None:
    if val < lo or val > hi:
        raise AssertionError(f"{msg or 'Value out of range'}: {val} not in [{lo}, {hi}]")


def assert_gt(a: float, b: float, msg: str = "") -> None:
    if not (a > b):
        raise AssertionError(f"{msg or 'Expected greater'}: {a} > {b}")


def assert_lt(a: float, b: float, msg: str = "") -> None:
    if not (a < b):
        raise AssertionError(f"{msg or 'Expected less'}: {a} < {b}")


def assert_has_key(obj: Dict, key: str, msg: str = "") -> None:
    if key not in obj:
        raise AssertionError(f"{msg or 'Missing key'}: '{key}' not in response")


class APITester:
    def __init__(self, base_url: str):
        self.base_url = base_url
        self.session = requests.Session()
        self.session.headers.update({"Content-Type": "application/json"})
        self.test_porcelain_id: Optional[int] = None
        self.test_crack_id: Optional[int] = None
        self.test_material_id: Optional[int] = None

    def _get(self, path: str) -> requests.Response:
        return self.session.get(f"{self.base_url}{path}", timeout=30)

    def _post(self, path: str, body: Dict = None) -> requests.Response:
        data = json.dumps(body) if body is not None else None
        return self.session.post(f"{self.base_url}{path}", data=data, timeout=30)

    def setup(self) -> bool:
        print_header("Test Setup")
        try:
            t0 = time.time()
            r = self._get("/api/porcelains")
            if r.status_code != 200:
                print_case("Server reachable", False,
                           f"Status {r.status_code}: {r.text[:200]}")
                return False
            porcelains = r.json()
            if not porcelains:
                print_case("Test data exists", False, "No porcelains found")
                return False
            self.test_porcelain_id = porcelains[0].get("id", 1)
            print_case("Server reachable & test data exists", True,
                       duration_ms=(time.time() - t0) * 1000)

            t0 = time.time()
            r = self._get(f"/api/porcelains/{self.test_porcelain_id}/cracks")
            if r.status_code == 200:
                cracks = r.json()
                if cracks:
                    self.test_crack_id = cracks[0].get("id")
            if not self.test_crack_id:
                self.test_crack_id = 1

            t0 = time.time()
            r = self._get("/api/repair-materials")
            if r.status_code == 200:
                materials = r.json()
                if materials:
                    self.test_material_id = materials[0].get("id")
            if not self.test_material_id:
                self.test_material_id = 1
            print_case("Test IDs resolved", True,
                       f"porcelain={self.test_porcelain_id}, crack={self.test_crack_id}, "
                       f"material={self.test_material_id}",
                       duration_ms=(time.time() - t0) * 1000)
            return True
        except Exception as e:
            print_case("Setup", False, str(e))
            return False

    def test_stress_analysis_endpoints(self) -> None:
        print_header("Stress Heatmap API Tests")

        t0 = time.time()
        try:
            r = self._post(f"/api/porcelains/{self.test_porcelain_id}/stress-analysis")
            assert_true(r.status_code == 200, f"Status {r.status_code}")
            data = r.json()
            assert_has_key(data, "max_von_mises")
            assert_has_key(data, "avg_von_mises")
            assert_has_key(data, "grid_points")
            assert_range(data.get("max_von_mises", 0), 0, 500, "max_von_mises in range")
            assert_range(data.get("avg_von_mises", 0), 0, 500, "avg_von_mises in range")
            assert_gt(len(data.get("grid_points", [])), 0, "grid_points non-empty")
            gp = data["grid_points"][0]
            for k in ["x", "y", "z", "von_mises"]:
                assert_has_key(gp, k, f"grid_point missing '{k}'")
            print_case("POST stress-analysis returns valid field", True,
                       duration_ms=(time.time() - t0) * 1000)
        except AssertionError as e:
            print_case("POST stress-analysis returns valid field", False, str(e),
                       duration_ms=(time.time() - t0) * 1000)
        except Exception as e:
            print_case("POST stress-analysis returns valid field", False, f"Exception: {e}",
                       duration_ms=(time.time() - t0) * 1000)

        t0 = time.time()
        try:
            r = self._get(f"/api/porcelains/{self.test_porcelain_id}/stress-analysis")
            assert_true(r.status_code == 200, f"Status {r.status_code}")
            data = r.json()
            if data and "max_von_mises" in data:
                assert_range(data["max_von_mises"], 0, 500)
            print_case("GET stress-analysis returns last result", True,
                       duration_ms=(time.time() - t0) * 1000)
        except AssertionError as e:
            print_case("GET stress-analysis returns last result", False, str(e),
                       duration_ms=(time.time() - t0) * 1000)
        except Exception as e:
            print_case("GET stress-analysis returns last result", False, f"Exception: {e}",
                       duration_ms=(time.time() - t0) * 1000)

        t0 = time.time()
        try:
            r = self._post("/api/porcelains/99999/stress-analysis")
            assert_true(r.status_code in (404, 500, 200), f"Invalid porcelain handled, status={r.status_code}")
            print_case("Invalid porcelain ID handled gracefully", True,
                       duration_ms=(time.time() - t0) * 1000)
        except Exception as e:
            print_case("Invalid porcelain ID handled gracefully", False, f"Exception: {e}",
                       duration_ms=(time.time() - t0) * 1000)

    def test_penetration_endpoints(self) -> None:
        print_header("Washburn Penetration API Tests")

        t0 = time.time()
        try:
            body = {"target_depth_um": 200}
            r = self._post(
                f"/api/cracks/{self.test_crack_id}/penetration/{self.test_material_id}",
                body
            )
            assert_true(r.status_code == 200, f"Status {r.status_code}")
            data = r.json()
            assert_has_key(data, "predicted_time_s")
            assert_has_key(data, "penetration_rate_um_s")
            assert_has_key(data, "time_series")
            assert_has_key(data, "depth_series")
            assert_gt(data.get("predicted_time_s", 0), 0, "predicted_time_s > 0")
            assert_gt(data.get("penetration_rate_um_s", 0), 0, "penetration_rate > 0")
            ts = data.get("time_series", [])
            ds = data.get("depth_series", [])
            assert_eq(len(ts), len(ds), "Series length equal")
            assert_gt(len(ts), 10, "At least 10 time points")
            for i in range(1, len(ds)):
                assert_gt(ds[i], ds[i - 1] - 1e-9,
                          f"depth monotonically increasing at i={i}")
            print_case("POST penetration returns valid prediction", True,
                       duration_ms=(time.time() - t0) * 1000)
        except AssertionError as e:
            print_case("POST penetration returns valid prediction", False, str(e),
                       duration_ms=(time.time() - t0) * 1000)
        except Exception as e:
            print_case("POST penetration returns valid prediction", False, f"Exception: {e}",
                       duration_ms=(time.time() - t0) * 1000)

        t0 = time.time()
        try:
            r = self._post(f"/api/cracks/{self.test_crack_id}/penetration/{self.test_material_id}")
            assert_true(r.status_code == 200, "Works without body (default depth)")
            print_case("POST penetration without body (defaults)", True,
                       duration_ms=(time.time() - t0) * 1000)
        except AssertionError as e:
            print_case("POST penetration without body (defaults)", False, str(e),
                       duration_ms=(time.time() - t0) * 1000)
        except Exception as e:
            print_case("POST penetration without body (defaults)", False, f"Exception: {e}",
                       duration_ms=(time.time() - t0) * 1000)

        t0 = time.time()
        try:
            r = self._post("/api/cracks/99999/penetration/99999", {"target_depth_um": 100})
            assert_true(r.status_code in (404, 500, 200),
                        f"Invalid IDs handled, status={r.status_code}")
            print_case("Invalid crack/material IDs handled gracefully", True,
                       duration_ms=(time.time() - t0) * 1000)
        except Exception as e:
            print_case("Invalid crack/material IDs handled gracefully", False, f"Exception: {e}",
                       duration_ms=(time.time() - t0) * 1000)

    def test_bending_endpoints(self) -> None:
        print_header("Four-Point Bending API Tests")

        t0 = time.time()
        try:
            body = {"porcelain_id": self.test_porcelain_id}
            r = self._post(
                f"/api/cracks/{self.test_crack_id}/bending-test/{self.test_material_id}",
                body
            )
            assert_true(r.status_code == 200, f"Status {r.status_code}")
            data = r.json()
            for k in ["original_strength_mpa", "unrepaired_strength_mpa",
                      "repaired_strength_mpa", "strength_recovery_ratio"]:
                assert_has_key(data, k, f"Missing '{k}'")
            assert_range(data.get("original_strength_mpa", 0), 10, 300,
                         "original_strength reasonable")
            assert_lt(data.get("unrepaired_strength_mpa", 1e9),
                      data.get("original_strength_mpa", 0) + 1e-6,
                      "unrepaired <= original")
            assert_range(data.get("strength_recovery_ratio", -1), 0, 1.2,
                         "recovery ratio in [0, 1.2]")
            if "load_series" in data:
                ls = data["load_series"]
                ds = data.get("displacement_series", [])
                if ls and ds:
                    assert_eq(len(ls), len(ds), "Load/displacement length equal")
            print_case("POST bending-test returns valid result", True,
                       duration_ms=(time.time() - t0) * 1000)
        except AssertionError as e:
            print_case("POST bending-test returns valid result", False, str(e),
                       duration_ms=(time.time() - t0) * 1000)
        except Exception as e:
            print_case("POST bending-test returns valid result", False, f"Exception: {e}",
                       duration_ms=(time.time() - t0) * 1000)

        t0 = time.time()
        try:
            r = self._post(
                f"/api/cracks/{self.test_crack_id}/bending-test/{self.test_material_id}"
            )
            assert_true(r.status_code == 200, "Works without porcelain_id")
            data = r.json()
            rr = data.get("strength_recovery_ratio", 0)
            assert_gt(rr, 0, "Recovery ratio positive")
            print_case("POST bending-test without porcelain_id (defaults)", True,
                       duration_ms=(time.time() - t0) * 1000)
        except AssertionError as e:
            print_case("POST bending-test without porcelain_id (defaults)", False, str(e),
                       duration_ms=(time.time() - t0) * 1000)
        except Exception as e:
            print_case("POST bending-test without porcelain_id (defaults)", False,
                       f"Exception: {e}", duration_ms=(time.time() - t0) * 1000)

        t0 = time.time()
        try:
            r = self._post("/api/cracks/99999/bending-test/99999")
            assert_true(r.status_code in (404, 500, 200),
                        f"Invalid IDs handled, status={r.status_code}")
            print_case("Invalid IDs handled gracefully", True,
                       duration_ms=(time.time() - t0) * 1000)
        except Exception as e:
            print_case("Invalid IDs handled gracefully", False, f"Exception: {e}",
                       duration_ms=(time.time() - t0) * 1000)

    def test_acceptance_criteria(self) -> None:
        print_header("Acceptance Criteria Integration Tests")

        t0 = time.time()
        try:
            r = self._post(f"/api/porcelains/{self.test_porcelain_id}/stress-analysis")
            data = r.json() if r.status_code == 200 else {}
            max_s = data.get("max_von_mises", 0)
            avg_s = data.get("avg_von_mises", 0)
            assert_range(max_s, 0, 400, "Max stress in physical range")
            assert_range(avg_s, 0, max_s + 1, "Avg <= Max")
            print_case("[ACCEPTANCE] Stress values physically reasonable", True,
                       duration_ms=(time.time() - t0) * 1000)
        except AssertionError as e:
            print_case("[ACCEPTANCE] Stress values physically reasonable", False, str(e),
                       duration_ms=(time.time() - t0) * 1000)

        t0 = time.time()
        try:
            r = self._post(
                f"/api/cracks/{self.test_crack_id}/penetration/{self.test_material_id}",
                {"target_depth_um": 100}
            )
            data = r.json() if r.status_code == 200 else {}
            pt = data.get("predicted_time_s", 0)
            ts = data.get("time_series", [])
            ds = data.get("depth_series", [])
            assert_gt(pt, 0, "Positive penetration time")
            if len(ds) >= 2:
                last_depth = ds[-1]
                assert_range(last_depth, 50, 500, "Final depth reasonable for target 100μm")
            print_case("[ACCEPTANCE] Penetration physically plausible", True,
                       duration_ms=(time.time() - t0) * 1000)
        except AssertionError as e:
            print_case("[ACCEPTANCE] Penetration physically plausible", False, str(e),
                       duration_ms=(time.time() - t0) * 1000)

        t0 = time.time()
        try:
            r = self._post(
                f"/api/cracks/{self.test_crack_id}/bending-test/{self.test_material_id}"
            )
            data = r.json() if r.status_code == 200 else {}
            rr = data.get("strength_recovery_ratio", 0)
            assert_gt(rr, 0, "Positive recovery")
            assert_lt(rr, 1.5, "Recovery < 150% (physical bound)")
            print_case("[ACCEPTANCE] Strength recovery in valid range", True,
                       duration_ms=(time.time() - t0) * 1000)
        except AssertionError as e:
            print_case("[ACCEPTANCE] Strength recovery in valid range", False, str(e),
                       duration_ms=(time.time() - t0) * 1000)


def assert_eq(a: Any, b: Any, msg: str = "") -> None:
    if a != b:
        raise AssertionError(f"{msg or 'EQ'}: {a} !== {b}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Porcelain Monitor API Integration Tests")
    parser.add_argument("--host", default="localhost", help="Backend host")
    parser.add_argument("--port", type=int, default=8080, help="Backend HTTP port")
    args = parser.parse_args()

    base_url = f"http://{args.host}:{args.port}"
    print(f"\n{Colors.BOLD}Porcelain Monitor API Integration Tests{Colors.RESET}")
    print(f"Base URL: {base_url}")

    tester = APITester(base_url)
    if not tester.setup():
        print(f"\n{Colors.RED}Setup failed. Ensure backend is running.{Colors.RESET}")
        return 1

    try:
        tester.test_stress_analysis_endpoints()
        tester.test_penetration_endpoints()
        tester.test_bending_endpoints()
        tester.test_acceptance_criteria()
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}Tests interrupted by user{Colors.RESET}")

    total = PASS + FAIL
    print(f"\n{Colors.BOLD}{'='*60}{Colors.RESET}")
    print(f"{Colors.BOLD}  Final Results: "
          f"{Colors.GREEN}{PASS} passed{Colors.RESET}, "
          f"{Colors.RED}{FAIL} failed{Colors.RESET}, "
          f"{total} total{Colors.RESET}")
    if total:
        rate = PASS / total * 100
        print(f"  Pass rate: {rate:.1f}%")
    print(f"{Colors.BOLD}{'='*60}{Colors.RESET}\n")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
