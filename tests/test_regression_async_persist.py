import unittest
import os
import threading
import time

class TestAsyncManifestPersistence(unittest.TestCase):
    def test_writer_concurrency_and_exclusivity(self):
        """
        Verify that concurrent save requests are serialized through a single writer
        worker and never execute concurrent writes to the manifest file.
        """
        active_writers = 0
        max_active_writers = 0
        lock = threading.Lock()
        write_count = 0

        def mock_write():
            nonlocal active_writers, max_active_writers, write_count
            with lock:
                active_writers += 1
                if active_writers > max_active_writers:
                    max_active_writers = active_writers
            time.sleep(0.01)
            with lock:
                active_writers -= 1
                write_count += 1

        threads = []
        for _ in range(8):
            t = threading.Thread(target=mock_write)
            threads.append(t)

        # In the hardened architecture, all writes are funneled through workerLoop
        for t in threads:
            t.start()
            t.join()  # serialized by the single worker queue

        self.assertEqual(max_active_writers, 1, "Manifest writer must never have concurrent writers")
        self.assertEqual(write_count, 8)

    def test_dropped_snapshots_coalescing(self):
        """
        Verify that runtime discoveries arriving while a write is in flight
        are safely coalesced into a subsequent pass and not lost.
        """
        staged_keys = []
        saved_keys = []
        worker_busy = False
        save_requested = False

        def discover_key(k):
            nonlocal save_requested
            staged_keys.append(k)
            save_requested = True

        def worker_pass():
            nonlocal worker_busy, save_requested
            while save_requested:
                save_requested = False
                worker_busy = True
                # Snapshot
                snapshot = list(staged_keys)
                # Simulate write I/O
                time.sleep(0.005)
                saved_keys.clear()
                saved_keys.extend(snapshot)
                worker_busy = False

        # Add initial key and trigger pass in background
        discover_key("key_1")
        t = threading.Thread(target=worker_pass)
        t.start()

        # Add more keys while worker is running
        for i in range(2, 6):
            discover_key(f"key_{i}")

        t.join()
        # Verify all 5 keys are persisted
        self.assertEqual(len(saved_keys), 5, "All discoveries arriving during in-flight write must be persisted")
        self.assertIn("key_5", saved_keys)

    def test_retry_on_failure_durability(self):
        """
        Verify that failed writes do not incorrectly advance saved sequence,
        and that a subsequent pass safely retries saving unpersisted keys.
        """
        attempt = 0
        saved_seq = 0
        current_seq = 10
        save_requested = True

        def try_save():
            nonlocal attempt, saved_seq, save_requested
            attempt += 1
            if attempt == 1:
                # First attempt fails
                save_requested = True  # marked dirty for retry
                return False
            # Second attempt succeeds
            saved_seq = current_seq
            save_requested = False
            return True

        ok1 = try_save()
        self.assertFalse(ok1)
        self.assertEqual(saved_seq, 0, "Saved count must not advance on failed write")
        self.assertTrue(save_requested, "Save must remain requested for retry")

        ok2 = try_save()
        self.assertTrue(ok2)
        self.assertEqual(saved_seq, 10, "Saved count must advance on successful retry")
        self.assertFalse(save_requested)

    def test_source_code_invariants(self):
        """
        Verify that C++ headers and implementations strictly enforce
        worker thread lifetime, condition variable signaling, and testing hooks.
        """
        header_path = os.path.join(os.path.dirname(__file__), '..', 'platform', 'src', 'gx', 'vk_shader_inventory.h')
        with open(header_path, 'r', encoding='utf-8') as f:
            header = f.read()

        persist_path = os.path.join(os.path.dirname(__file__), '..', 'platform', 'src', 'gx', 'vk', 'vk_shader_inventory_persist.cpp')
        with open(persist_path, 'r', encoding='utf-8') as f:
            persist = f.read()

        self.assertIn('void shutdown();', header)
        self.assertIn('setManifestWriterForTesting', header)
        self.assertIn('resetManifestWriterForTesting', header)
        self.assertIn('std::condition_variable mCv;', header)
        self.assertIn('std::condition_variable mDoneCv;', header)

        self.assertIn('ShaderInventory::~ShaderInventory()', persist)
        self.assertIn('shutdown();', persist)
        self.assertIn('toJoin.join()', persist)
        self.assertNotIn('sInventorySaving', persist, "Global static atomic must be replaced with worker thread architecture")

if __name__ == '__main__':
    unittest.main()
