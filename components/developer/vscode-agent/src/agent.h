/* agent.h -- vscode-agent: runs programs for VS Code on AMIX */
#ifndef AGENT_H
#define AGENT_H

#include <stddef.h>
#include <stdint.h>

#define AGENT_VERSION	"0.1.1"
#define PROTO_VERSION	1

struct sha256 {
	uint32_t h[8];
	uint8_t buf[64];
	size_t n;
	uint32_t lo, hi;	/* bytes hashed */
};

void sha256_init(struct sha256 *);
void sha256_update(struct sha256 *, const void *, size_t);
void sha256_final(struct sha256 *, uint8_t[32]);
void hmac_sha256(const void *, size_t, const void *, size_t, uint8_t[32]);
void tohex(const uint8_t *, size_t, char *);

#endif
