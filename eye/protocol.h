// Parser for the Pi -> Arduino tracking messages (see pi/PROTOCOL.md).
// Plain C++ with no Arduino calls, so it can also be tested on a computer.
//
// One message per line: <STATE>,<PAN>,<TILT>
//   STATE  T (tracking), H (holding), or L (lost)
//   PAN    whole degrees 0-180
//   TILT   whole degrees 0-180
// Examples: "T,94,90"  "H,94,90"  "L,90,90"

#ifndef WATCHER_PROTOCOL_H
#define WATCHER_PROTOCOL_H

struct TrackMessage {
  char state;  // 'T', 'H', or 'L'
  int pan;
  int tilt;
};

// Longest valid line is "L,180,180" (9 characters). Anything longer is rejected.
const unsigned char MESSAGE_MAX_LEN = 9;

// Reads 1-3 digits worth 0-180 and advances p past them.
inline bool parseAngle(const char*& p, int& out) {
  int value = 0;
  int digits = 0;
  while (*p >= '0' && *p <= '9') {
    if (++digits > 3) return false;
    value = value * 10 + (*p - '0');
    p++;
  }
  if (digits == 0 || value > 180) return false;
  out = value;
  return true;
}

// Returns true only for a complete, exactly formatted message.
// Signs, spaces, decimals, extra fields, and trailing characters are rejected.
inline bool parseMessage(const char* line, TrackMessage& msg) {
  char state = line[0];
  if (state != 'T' && state != 'H' && state != 'L') return false;
  if (line[1] != ',') return false;

  const char* p = line + 2;
  int pan, tilt;
  if (!parseAngle(p, pan)) return false;
  if (*p != ',') return false;
  p++;
  if (!parseAngle(p, tilt)) return false;
  if (*p != '\0') return false;

  msg.state = state;
  msg.pan = pan;
  msg.tilt = tilt;
  return true;
}

#endif
