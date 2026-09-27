#include "console.h"

namespace
{
const int MAX_MIRRORS = 3;

class TeePrint : public Print
{
public:
  Print *mirrors[MAX_MIRRORS] = {};
  int n = 0;
  size_t write(uint8_t c) override
  {
    Serial.write(c);
    for (int i = 0; i < n; i++)
      mirrors[i]->write(c);
    return 1;
  }
  size_t write(const uint8_t *buf, size_t len) override
  {
    Serial.write(buf, len);
    for (int i = 0; i < n; i++)
      mirrors[i]->write(buf, len);
    return len;
  }
};

TeePrint tee;
bool (*pendingChecks[MAX_MIRRORS])() = {};
} // namespace

Print &Console = tee;

void consoleAddMirror(Print *mirror, bool (*inputPending)())
{
  if (tee.n >= MAX_MIRRORS)
    return;
  pendingChecks[tee.n] = inputPending;
  tee.mirrors[tee.n++] = mirror;
}

bool consoleAbortRequested()
{
  // Se ignoran los saltos de línea que algunos monitores envían tras la tecla
  bool pressed = false;
  while (Serial.available())
  {
    char c = Serial.read();
    if (c != '\r' && c != '\n')
      pressed = true;
  }
  for (int i = 0; i < tee.n; i++)
    if (pendingChecks[i] && pendingChecks[i]())
      pressed = true;
  return pressed;
}
