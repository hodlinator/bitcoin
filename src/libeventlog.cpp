// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <libeventlog.h>
#include <logging.h>

#include <event2/event.h>

/** libevent event log callback */
static void libevent_log_cb(int severity, const char *msg)
{
    switch (severity) {
    case EVENT_LOG_DEBUG:
        LogDebug(BCLog::LIBEVENT, "%s", msg);
        break;
    case EVENT_LOG_MSG:
        LogInfo("libevent: %s", msg);
        break;
    case EVENT_LOG_WARN:
        LogWarning("libevent: %s", msg);
        break;
    default: // EVENT_LOG_ERR and others are mapped to error
        LogError("libevent: %s", msg);
        break;
    }
}

void InitLibEventLogging()
{
    // Redirect libevent's logging to our own log
    event_set_log_callback(&libevent_log_cb);
    // Update libevent's log handling.
    UpdateLibEventLogging(LogInstance().WillLogCategory(BCLog::LIBEVENT));
}

void UpdateLibEventLogging(bool enable)
{
    if (enable) {
        event_enable_debug_logging(EVENT_DBG_ALL);
    } else {
        event_enable_debug_logging(EVENT_DBG_NONE);
    }
}
