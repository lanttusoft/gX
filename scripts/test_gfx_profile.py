#ai: native timing contracts pin enum/table identity, unchanged pass execution order and frame-matched benchmark windows.
import csv
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

import gfx_benchmark as benchmark


ROOT = Path(__file__).resolve().parents[1]
#ai: native/CSV probes share the harness-neutral scratch override used by existing audits.
SCRATCH = benchmark.audit.SCRATCH
SCRATCH.mkdir(parents=True, exist_ok=True)


def pass_contract(source):
    enum = source[source.index("typedef enum gX_GFX_PassId"):source.index("} gX_GFX_PassId;")]
    identifiers = re.findall(r"^\s*(PASS_[A-Z_]+)", enum, re.M)
    table = source[source.index("static const gX_GFX_Pass s_gfxPasses"):source.index("static gX_I0 Pass_RunRange")]
    names = re.findall(r'\{"([a-z_]+)"', table)
    return identifiers[:-1], names


class ProfilePassContract(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / "src/renderer/gfx/gfx_pipeline.c").read_text()
        self.identifiers, self.names = pass_contract(self.source)

    def test_enum_matches_execution_table(self):
        self.assertEqual([name.removeprefix("PASS_").lower() for name in self.identifiers], self.names)

    def test_every_pass_executes_once_in_existing_order(self):
        draw = self.source[self.source.index("gX_I0 gX_RendererGFX_DrawTo("):]
        ranges = re.findall(r"Pass_RunRange\(gfx, outputRT, (PASS_[A-Z_]+), (PASS_[A-Z_]+)\)", draw)
        identifiers = self.identifiers + ["PASS_COUNT"]
        indices = []
        for start, end in ranges:
            self.assertLess(identifiers.index(start), identifiers.index(end))
            indices.extend(range(identifiers.index(start), identifiers.index(end)))
        self.assertEqual(indices, list(range(len(self.names))))
        self.assertEqual(self.names[9:15], ["labels", "transparent", "oracle_gpu", "ssr_inputs", "ssr", "ssr_vis"])

    def test_ssr_timer_contains_only_actual_ssr_draw(self):
        draw = self.source[self.source.index("gX_I0 gX_RendererGFX_DrawTo("):]
        begin = draw.index("gX_RHI_Query_Timer_Begin(&gfx->timerSSR)")
        end = draw.index("gX_RHI_Query_Timer_End(&gfx->timerSSR)", begin)
        ranges = re.findall(r"Pass_RunRange\(gfx, outputRT, (PASS_[A-Z_]+), (PASS_[A-Z_]+)\)", draw[begin:end])
        self.assertEqual(len(ranges), 1)
        start, finish = ranges[0]
        self.assertEqual(self.names[self.identifiers.index(start):self.identifiers.index(finish)], ["ssr"])

    def test_frame_profile_is_opt_in(self):
        self.assertRegex(self.source, r"if \(arguments\.gfxProfile && !gfx->profileAttempted\)")

    def test_all_profile_intervals_are_bracketed_once(self):
        begin = re.findall(r"Profile_StageBegin\(gfx->profile, (GX_GFX_PROFILE_[A-Z_]+)\)", self.source)
        end = re.findall(r"Profile_StageEnd\(gfx->profile, (GX_GFX_PROFILE_[A-Z_]+)\)", self.source)
        self.assertEqual(begin, end)
        self.assertEqual(len(begin), 9)
        self.assertEqual(len(set(begin)), 9)


class ProfileFrameWindow(unittest.TestCase):
    def rows(self):
        return [dict(frame=i, draw=i - 1, readback_frame=i + 2, total_ms=10.0, ssr_ms=8.0) for i in range(1, 8)]

    def read(self, rows, columns=None):
        with tempfile.TemporaryDirectory(prefix="gfx_profile_csv_", dir=SCRATCH) as directory:
            path = Path(directory) / "gpu.csv"
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=columns or list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            return benchmark.read_gpu_window(path, 2, 3)

    def test_source_frame_window_not_csv_tail(self):
        values, metadata = self.read(self.rows())
        self.assertEqual(values, [10.0] * 3)
        self.assertEqual(metadata["frame_ids"], [3, 4, 5])
        self.assertEqual(metadata["samples"], 3)

    def test_out_of_order_readbacks_are_sorted_by_source_frame(self):
        values, metadata = self.read(list(reversed(self.rows())))
        self.assertEqual(metadata["frame_ids"], [3, 4, 5])
        self.assertEqual(len(values), 3)

    def test_missing_source_frame_fails(self):
        with self.assertRaisesRegex(RuntimeError, "missing"):
            self.read([r for r in self.rows() if r["frame"] != 4])

    def test_duplicate_frame_fails_instead_of_counting_stale_rows(self):
        rows = self.rows()
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            self.read(rows + [dict(rows[3])])

    def test_multiple_draws_per_frame_require_explicit_aggregation(self):
        rows = self.rows()
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            self.read(rows + [dict(rows[3], draw=55)])

    def test_legacy_profile_has_no_frame_identity_contract(self):
        with self.assertRaisesRegex(RuntimeError, "frame identity"):
            self.read([dict(frame=i, total_ms=10) for i in range(1, 8)])

    def test_readback_cannot_precede_source_frame(self):
        rows = self.rows()
        rows[3]["readback_frame"] = 1
        with self.assertRaisesRegex(RuntimeError, "readback"):
            self.read(rows)

    def test_duplicate_draw_ids_fail(self):
        rows = self.rows()
        rows[3]["draw"] = rows[2]["draw"]
        with self.assertRaisesRegex(RuntimeError, "draw"):
            self.read(rows)

    def test_non_finite_or_negative_times_fail(self):
        for value in (float("nan"), float("inf"), -1.0):
            with self.subTest(value=value):
                rows = self.rows()
                rows[3]["ssr_ms"] = value
                with self.assertRaisesRegex(RuntimeError, "timing"):
                    self.read(rows)

    def test_incomplete_timing_row_fails(self):
        rows = self.rows()
        rows[3]["ssr_ms"] = None
        with self.assertRaisesRegex(RuntimeError, "timing"):
            self.read(rows)


class ProfileCpuWindow(unittest.TestCase):
    def read(self, rows):
        with tempfile.TemporaryDirectory(prefix="gfx_cpu_window_", dir=SCRATCH) as directory:
            path = Path(directory) / "cpu.csv"
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["frame", "cpu_ms"])
                writer.writeheader()
                writer.writerows(rows)
            return benchmark.read_cpu_window(path, 2, 3)

    def test_drain_frames_are_not_measured(self):
        rows = [dict(frame=i, cpu_ms=float(i)) for i in range(1, 8)]
        self.assertEqual(self.read(rows), [3.0, 4.0, 5.0])

    def test_missing_or_duplicate_measured_frames_fail(self):
        rows = [dict(frame=i, cpu_ms=1.0) for i in range(1, 8)]
        with self.assertRaisesRegex(RuntimeError, "missing"):
            self.read([r for r in rows if r["frame"] != 4])
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            self.read(rows + [dict(rows[3])])

    def test_non_finite_cpu_timing_fails(self):
        rows = [dict(frame=i, cpu_ms=1.0) for i in range(1, 8)]
        rows[3]["cpu_ms"] = float("nan")
        with self.assertRaisesRegex(RuntimeError, "timing"):
            self.read(rows)


#ai: compile the actual collector with owned RHI/allocation stubs; no GPU or duplicate model of its ring logic.
class NativeProfileCollector(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="gfx_profile_native_", dir=SCRATCH)
        cls.directory = Path(cls.temporary.name)
        cls.executable = cls.directory / "collector"
        source = (ROOT / "src/renderer/gfx/profile.c").read_text()
        source = re.sub(r"^#include[^\n]*\n", "", source, flags=re.M)
        prelude = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PROFILE_HEADER"
#include "QUERY_HEADER"
#define gX_RHI_TIMESTAMP 0x8E28
static unsigned InitCalls,DeleteCalls,AllocCalls,FreeCalls,NextId=1,BlockedId;
static int InitFailAt,AllocationFails,Ready,Unavailable,LogCount;
static void *TestAlloc(unsigned amount) {AllocCalls++;return AllocationFails ? NULL : calloc(1,amount);}
static int TestFree(void *pointer) {FreeCalls++;free(pointer);return 0;}
#define gX_Calloc TestAlloc
#define gX_Free TestFree
#define gX_TraceLog_GFX(...) ((void)LogCount++)
gX_I1 gX_RHI_Query_IsAvailable(gX_U32 target) {(void)target;return !Unavailable;}
gX_I32 gX_RHI_Query_Timer_Init(gX_RHI_Query_Timer *timer,const gX_I8 *name)
{
    (void)name;
    InitCalls++;
    if (InitFailAt && InitCalls==(unsigned)InitFailAt) return -1;
    timer->queryIds[0]=NextId++;
    timer->queryIds[1]=NextId++;
    return 0;
}
gX_I0 gX_RHI_Query_Timer_Begin(gX_RHI_Query_Timer *timer)
{
    assert(!timer->started || timer->available);
    timer->started=1;timer->available=0;timer->active=1;
}
gX_I0 gX_RHI_Query_Timer_End(gX_RHI_Query_Timer *timer)
{
    assert(timer->active);timer->active=0;
}
gX_I1 gX_RHI_Query_Timer_Get(gX_RHI_Query_Timer *timer)
{
    if (!timer->started || timer->active || !Ready || timer->queryIds[0]==BlockedId) return 0;
    timer->available=1;timer->elapsedMs=(double)timer->queryIds[0]*0.001;return 1;
}
gX_I0 gX_RHI_Query_Timer_Deinit(gX_RHI_Query_Timer *timer)
{
    assert(timer->queryIds[0]);DeleteCalls++;
}
'''.replace("PROFILE_HEADER", str(ROOT / "src/renderer/gfx/profile.h")).replace("QUERY_HEADER", str(ROOT / "src/rhi/rhi_query.h"))
        body = r'''
static void draw(gX_RendererGFX_Profile *profile,unsigned frame)
{
    gX_RendererGFX_Profile_Begin(profile,frame);
    for (unsigned stage=0;stage<GX_GFX_PROFILE_TOTAL;stage++)
    {
        gX_RendererGFX_Profile_StageBegin(profile,stage);
        gX_RendererGFX_Profile_StageEnd(profile,stage);
    }
    gX_RendererGFX_Profile_End(profile);
}
int main(int argc,char **argv)
{
    assert(argc==3);
    int mode=atoi(argv[1]);
    if (mode==1) InitFailAt=7;
    if (mode==2) AllocationFails=1;
    if (mode==4) Unavailable=1;
    gX_RendererGFX_Profile *profile=gX_RendererGFX_Profile_Init(argv[2]);
    if (mode>=1 && mode<=4)
    {
        assert(!profile);
        assert(DeleteCalls==(mode==1 ? 6u : 0u));
        assert(FreeCalls==((mode==1 || mode==3) ? 1u : 0u));
        assert(LogCount==1);
        return 0;
    }
    assert(profile);
    if (mode==5)
    {
        draw(profile,1);
        gX_RendererGFX_Profile_Deinit(profile,2);
        assert(LogCount==1 && DeleteCalls==GX_GFX_PROFILE_RING*GX_GFX_PROFILE_STAGES);
        return 0;
    }
    if (mode==6)
    {
        draw(profile,1);Ready=1;
        gX_RendererGFX_Profile_Begin(profile,2);
        assert(profile->fileFailed && profile->current<0);
        assert(LogCount>=1);
        gX_RendererGFX_Profile_Deinit(profile,2);
        return 0;
    }
    for (unsigned frame=1;frame<=GX_GFX_PROFILE_RING;frame++) draw(profile,frame);
    gX_RendererGFX_Profile_Begin(profile,17);
    assert(profile->current<0 && profile->dropped==1);
    assert(profile->slots[0].frame==1 && profile->slots[0].pending);
    Ready=1;
    BlockedId=profile->slots[0].timers[GX_GFX_PROFILE_SSR].queryIds[0];
    gX_RendererGFX_Profile_Collect(profile,18);
    assert(profile->slots[0].pending);
    for (unsigned slot=1;slot<GX_GFX_PROFILE_RING;slot++) assert(!profile->slots[slot].pending);
    BlockedId=0;
    draw(profile,18);
    draw(profile,18);
    gX_RendererGFX_Profile_Deinit(profile,19);
    assert(DeleteCalls==GX_GFX_PROFILE_RING*GX_GFX_PROFILE_STAGES && FreeCalls==1);
    return 0;
}
'''
        path = cls.directory / "probe.c"
        path.write_text(prelude + source + body)
        result = subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(cls.executable)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_case(self, mode, path=None):
        path = path or self.directory / ("case_%d.csv" % mode)
        environment = dict(os.environ)
        environment.pop("GX_BENCHMARK_NO_FLUSH", None)
        result = subprocess.run([str(self.executable), str(mode), str(path)], capture_output=True, text=True, env=environment)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return path

    def test_complete_sets_pending_pairs_overflow_and_same_frame_draws(self):
        path = self.run_case(0)
        with path.open() as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(len(rows), 18)
        self.assertEqual(len({row["draw"] for row in rows}), 18)
        self.assertEqual(sum(int(row["frame"]) == 18 for row in rows), 2)
        self.assertNotIn("17", {row["frame"] for row in rows})
        self.assertEqual([row["frame"] for row in rows][-3], "1")
        self.assertTrue(all(int(row["readback_frame"]) >= int(row["frame"]) for row in rows))

    def test_partial_query_initialization_rolls_back(self):
        self.run_case(1)

    def test_allocation_failure_does_not_allocate_queries(self):
        self.run_case(2)

    def test_invalid_output_path_rolls_back_allocation(self):
        self.run_case(3, self.directory / "missing" / "gpu.csv")

    def test_unsupported_queries_do_not_allocate(self):
        self.run_case(4)

    def test_pending_tail_is_not_fabricated_or_forced_ready(self):
        path = self.run_case(5)
        with path.open() as stream:
            self.assertEqual(list(csv.DictReader(stream)), [])

    def test_csv_write_failure_stops_new_captures(self):
        self.run_case(6, Path("/dev/full"))


if __name__ == "__main__":
    unittest.main()
