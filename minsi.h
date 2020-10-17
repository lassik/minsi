// Event types:
//
// '^' -- control character     "@" "A" ... "?"
// 'c' -- character             "ö"
// 'e' -- escape sequence       "[1;10C"
// 'm' -- mouse event           ""
// 'r' -- resize                ""

struct minsi;

struct minsi *minsiFromFd(int fd);
struct minsi *minsiFromStdin(void);
struct minsi *minsiFromStdout(void);
int minsiSwitchToRawMode(struct minsi *minsi);
int minsiSwitchToOrigMode(struct minsi *minsi);
int minsiGetSize(struct minsi *minsi, int *out_x, int *out_y);
const char *minsiReadEvent(struct minsi *minsi);
int minsiWriteFlush(struct minsi *minsi);
void minsiWriteString(struct minsi *minsi, const char *string);
void minsiWriteEscape(struct minsi *minsi, const char *string);
void minsiSetResizeFlag(struct minsi *minsi);
