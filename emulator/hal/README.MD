# Hardware Abstraction Layer (Android Device Emulation)

This directory houses the host-side implementation of Android guest drivers. These drivers facilitate communication with the guest operating system and expose high-level interfaces for manipulating emulated devices. For example, developers can programmatically access the guest's clipboard and register for notifications when its contents change.

**Key Considerations:**

* **Separation of Concerns:** These drivers are exclusively responsible for communication with the guest operating system. Avoid implementing business logic within them. For instance, the GuestStatus driver tracks the boot status of the guest and notifies listeners; it should not initiate actions like APK installation or window resizing. Such logic belongs in higher-level components.

* **Vsock Preference:** When adding new emulated devices, prioritize vsock over qemu-based communication. Vsock's architecture facilitates stubbing for testing purposes, enhancing testability and modularity.

* **Testing:** Comprehensive testing of these drivers is crucial to ensure the stability and reliability of the emulated environment.  Leverage the testability afforded by vsock to create unit ests that validate driver behavior and communication with the guest operating system.
