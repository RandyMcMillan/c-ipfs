#pragma once

/**
 * Download an IPFS object to a file.
 * Usage: ipfs get <hash> [<output-file>]
 */
int ipfs_get(int argc, char** argv);
