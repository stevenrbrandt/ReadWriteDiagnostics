To use this thorn, add the following to your thornlist

  !TARGET  = $ARR
  !TYPE = git
  !URL = https://github.com/stevenrbrandt/ReadWriteDiagnostics.git
  !REPO_PATH=$2
  !CHECKOUT =
  ReadWriteDiagnostics/ReadWriteDiagnostic

Important environment variables:

RDWR_DEBUG_VARS: A list of variables to trace during execution.

RDWR_DEBUG_INDEXES: An x,y,z tuple. For each variable in RDWR_DEBUG_VARS print out the value at position x,y,z (defaults to 0,0,0).
