#include "wifi_list.h"

#include <string.h>

static bool lengths_ok(const char *ssid, const char *pass) {
  size_t s = strlen(ssid), p = strlen(pass);
  return s >= 1 && s <= WIFI_SSID_MAX_LEN && (p == 0 || (p >= WIFI_PASS_MIN_LEN && p <= WIFI_PASS_MAX_LEN));
}

void wifi_list_clear(wifi_list_t *l) { memset(l, 0, sizeof(*l)); }

bool wifi_list_put(wifi_list_t *l, const char *ssid, const char *pass) {
  if (!lengths_ok(ssid, pass)) return false;

  int idx = -1;
  for (int i = 0; i < l->count; i++)
    if (strcmp(l->net[i].ssid, ssid) == 0) idx = i;

  if (idx >= 0) {
    /* Mạng đã có: đưa xuống cuối (mới nhất) với mật khẩu mới. */
    memmove(&l->net[idx], &l->net[idx + 1], (size_t)(l->count - idx - 1) * sizeof(wifi_network_t));
    l->count--;
  } else if (l->count == WIFI_LIST_MAX) {
    memmove(&l->net[0], &l->net[1], (WIFI_LIST_MAX - 1) * sizeof(wifi_network_t));
    l->count--;
  }

  wifi_network_t *n = &l->net[l->count++];
  memset(n, 0, sizeof(*n));
  strcpy(n->ssid, ssid);
  strcpy(n->pass, pass);
  return true;
}

bool wifi_list_valid(const wifi_list_t *l) {
  if (l->count > WIFI_LIST_MAX) return false;
  for (int i = 0; i < l->count; i++) {
    const wifi_network_t *n = &l->net[i];
    if (memchr(n->ssid, 0, sizeof(n->ssid)) == NULL || memchr(n->pass, 0, sizeof(n->pass)) == NULL) return false;
    if (!lengths_ok(n->ssid, n->pass)) return false;
  }
  return true;
}
