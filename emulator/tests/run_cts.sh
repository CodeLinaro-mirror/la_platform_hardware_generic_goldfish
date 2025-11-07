#/bin/bash

# show steps and exit on any shell error
set -x
set -e

#set

# Bazel provides paths to individual dependencies inside of the .zip
# archives (see cts.bzl for how these are setup). In this context, we
# want to top-level directory.  The logic below converts the path-to-artifact
# to a directory.

if [[ -d "hardware/generic/goldfish/emulator/tests/local/image" ]]; then
  IMAGE_EXTRACT_DIR=$(pwd)/hardware/generic/goldfish/emulator/tests/local/image
  echo "Using a LOCAL Image: $IMAGE_EXTRACT_DIR"
else
  IMAGE_EXTRACT_DIR=$(readlink -f $(dirname $(dirname $(pwd)/$IMAGE_PATH)))
fi
BUILD_TOOLS_EXTRACT_DIR=$(readlink -f $(dirname $(dirname $(pwd)/$BUILD_TOOLS_PATH)))
PLATFORM_TOOLS_EXTRACT_DIR=$(readlink -f $(dirname $(dirname $(pwd)/$PLATFORM_TOOLS_PATH)))
TRADEFED_EXTRACT_DIR=$(readlink -f $(dirname $(dirname $(dirname $(pwd)/$CTS_TRADEFED_PATH))))
TEST_SEQ_DIR=$(readlink -f $(dirname $(pwd)/$TEST_SEQ_PATH))

# Copy the emulator zip target (which is a data dependency for the test)
# to temp storage and unzip it.
EMULATOR_EXTRACTDIR=$TEST_TMPDIR/emulator
mkdir -p EMULATOR_EXTRACTDIR
unzip -q hardware/generic/goldfish/emulator/sdk-repo-linux--developer.zip \
    -d $EMULATOR_EXTRACTDIR

# Tradefed (android-cts) is currently located in the deps dir, but trying
# to run it directly from there results in an error:
#   ...jdk/bin/java: Argument list too long
# A workaround that seems ok for now is to symlink android-cts to $TEST_TMPDIR
# and run it from there
ln -s $TRADEFED_EXTRACT_DIR/android-cts $TEST_TMPDIR/android-cts

# A text proto named sequence.txtpb contains configuration for tradefed and the
# emulator. Being a text proto file, it can be easily generated in various ways.
# For now, sed is being used to update paths to ones provided by the test
# framework.
SEQUENCE_TXTPB=$TEST_TMPDIR/sequence.txtpb
cp hardware/generic/goldfish/emulator/tests/sequence.txtpb $SEQUENCE_TXTPB
sed -i "s,TEST_TMPDIR,$TEST_TMPDIR," $SEQUENCE_TXTPB
sed -i "s,TEST_SPEC,$TEST_SPEC," $SEQUENCE_TXTPB
sed -i "s,IMAGE_EXTRACT_DIR,$IMAGE_EXTRACT_DIR," $SEQUENCE_TXTPB
sed -i "s,BUILD_TOOLS_EXTRACT_DIR,$BUILD_TOOLS_EXTRACT_DIR," $SEQUENCE_TXTPB
sed -i "s,PLATFORM_TOOLS_EXTRACT_DIR,$PLATFORM_TOOLS_EXTRACT_DIR," $SEQUENCE_TXTPB
sed -i "s,PWD,$(pwd)," $SEQUENCE_TXTPB

# test_seq needs HOME set so that it can create an adb keyfile.
export HOME=$TEST_TMPDIR/home
mkdir $HOME

# Run the test
cd $TEST_SEQ_DIR
./test_seq \
    --name $TEST_UNDECLARED_OUTPUTS_DIR/results \
    --runtime_dir $TEST_TMPDIR/runtime \
    $SEQUENCE_TXTPB

