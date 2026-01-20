# Feature Flow: <Feature Name>

**Goal:** <What is being accomplished?> (e.g., Screenshot Streaming)
**Entry Point:** `<File:Line>`
**Exit Point:** `<File:Line>`

## The "Red Thread" Diagram

## Integration Seams

* **Injection Point A:** `<File:Line>` - Good place to hook shared memory.
* **Injection Point B:** `<File:Line>` - Good place to add logging.

## Threading & Safety

* **Critical Path:** Executed on `<ThreadName>`.
* **Locks:** `<LockName>` is held during steps X-Y.