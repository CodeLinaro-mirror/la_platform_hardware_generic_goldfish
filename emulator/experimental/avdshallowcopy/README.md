# avdshallowcopy: shallow copy an AVD

## Description

The tool creates shallow copies of Android Virtual Devices (AVDs).

It utilizes the qcow2 file feature to track changes on top of changes
(i.e. another qcow2 file). In this case, the backing qcow2 file is
located in the source AVD.

Warning: once a shallow AVD is created, the source AVD cannot change.

Note: snapshots are not yet supported.

## Usage

avdshallowcopy <AVD_DIRECTORY> <SRC_NAME> <DST_NAME>

Example: avdshallowcopy ~/.android/avd Medium_Phone Medium_Phone.shallow

Arguments:

* SRC_NAME - the source AVD name to copy from, it must exist.
* DST_NAME - the destination AVD name to copy into, it must not exist.

Exit codes: zero - success, non zero - an error.