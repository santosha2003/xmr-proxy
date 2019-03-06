/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2017-2018 XMR-Stak    <https://github.com/fireice-uk>, <https://github.com/psychocrypt>
 * Copyright 2016-2018 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <limits.h>
#include <string.h>
#include <uv.h>


#include "common/config/ConfigLoader.h"
#include "common/log/Log.h"
#include "common/xmrig.h"
#include "core/Config.h"
#include "core/ConfigCreator.h"
#include "donate.h"
#include "rapidjson/document.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"


static const char *modeNames[] = {
    "nicehash",
    "simple"
};


#if defined(_WIN32) && !defined(strncasecmp)
#   define strncasecmp _strnicmp
#endif


xmrig::Config::Config() : xmrig::CommonConfig(),
    m_debug(false),
    m_ready(false),
    m_verbose(false),
    m_mode(NICEHASH_MODE),
    m_reuseTimeout(0),
    m_diff(0),
    m_workersMode(Workers::RigID)
{
}


bool xmrig::Config::isTLS() const
{
#   ifndef XMRIG_NO_TLS
    for (const BindHost &host : m_bind) {
        if (host.isTLS()) {
            return true;
        }
    }
#   endif

    return false;
}


bool xmrig::Config::reload(const char *json)
{
    return xmrig::ConfigLoader::reload(this, json);
}


const char *xmrig::Config::modeName() const
{
    return modeNames[m_mode];
}


void xmrig::Config::getJSON(rapidjson::Document &doc) const
{
    using namespace rapidjson;

    doc.SetObject();

    auto &allocator = doc.GetAllocator();

    doc.AddMember("access-log-file", accessLog() ? Value(StringRef(accessLog())).Move() : Value(kNullType).Move(), allocator);
    doc.AddMember("algo",            StringRef(algorithm().name()), allocator);

    Value api(kObjectType);
    api.AddMember("port",         apiPort(), allocator);
    api.AddMember("access-token", apiToken() ? Value(StringRef(apiToken())).Move() : Value(kNullType).Move(), allocator);
    api.AddMember("id",           apiId() ? Value(StringRef(apiId())).Move() : Value(kNullType).Move(), allocator);
    api.AddMember("worker-id",    apiWorkerId() ? Value(StringRef(apiWorkerId())).Move() : Value(kNullType).Move(), allocator);
    api.AddMember("ipv6",         isApiIPv6(), allocator);
    api.AddMember("restricted",   isApiRestricted(), allocator);
    doc.AddMember("api",          api, allocator);

    doc.AddMember("background",   isBackground(), allocator);

    Value bind(kArrayType);
    for (const xmrig::BindHost &host : m_bind) {
        bind.PushBack(host.toJSON(doc), allocator);
    }

    doc.AddMember("bind",          bind, allocator);
    doc.AddMember("colors",        isColors(), allocator);
    doc.AddMember("custom-diff",   diff(), allocator);
    doc.AddMember("donate-level",  donateLevel(), allocator);
    doc.AddMember("log-file",      logFile() ? Value(StringRef(logFile())).Move() : Value(kNullType).Move(), allocator);
    doc.AddMember("mode",          StringRef(modeName()), allocator);
    doc.AddMember("pools",         m_pools.toJSON(doc), allocator);
    doc.AddMember("retries",       m_pools.retries(), allocator);
    doc.AddMember("retry-pause",   m_pools.retryPause(), allocator);
    doc.AddMember("reuse-timeout", reuseTimeout(), allocator);

#   ifndef XMRIG_NO_TLS
    doc.AddMember("tls", m_tls.toJSON(doc), allocator);
#   endif

    doc.AddMember("user-agent",    userAgent() ? Value(StringRef(userAgent())).Move() : Value(kNullType).Move(), allocator);

#   ifdef HAVE_SYSLOG_H
    doc.AddMember("syslog", isSyslog(), allocator);
#   endif

    doc.AddMember("verbose",      isVerbose(), allocator);
    doc.AddMember("watch",        m_watch,     allocator);
    doc.AddMember("workers",      Workers::modeToJSON(workersMode()), allocator);
}


xmrig::Config *xmrig::Config::load(Process *process, IConfigListener *listener)
{
    return static_cast<Config*>(ConfigLoader::load(process, new ConfigCreator(), listener));
}


bool xmrig::Config::finalize()
{
    if (m_state != NoneState) {
        return CommonConfig::finalize();
    }

    if (!CommonConfig::finalize()) {
        return false;
    }

    if (m_bind.empty()) {
        m_bind.push_back(xmrig::BindHost("0.0.0.0", 3333, 4));
        m_bind.push_back(xmrig::BindHost("::", 3333, 6));
    }

    return true;
}


bool xmrig::Config::parseBoolean(int key, bool enable)
{
    if (!CommonConfig::parseBoolean(key, enable)) {
        return false;
    }

    switch (key) {
    case VerboseKey: /* --verbose */
        m_verbose = enable;
        break;

    case DebugKey: /* --debug */
        m_debug = enable;
        break;

    case WorkersKey: /* workers */
    case WorkersAdvKey:
        m_workersMode = enable ? Workers::RigID : Workers::None;
        break;

    default:
        break;
    }

    return true;
}


bool xmrig::Config::parseString(int key, const char *arg)
{
    if (!CommonConfig::parseString(key, arg)) {
        return false;
    }

    switch (key) {
    case ModeKey: /* --mode */
        setMode(arg);
        break;

    case BindKey:    /* --bind */
    case TlsBindKey: /* --tls-bind */
        {
            xmrig::BindHost host(arg);
            host.setTLS(key == TlsBindKey);

            if (host.isValid()) {
                m_bind.push_back(std::move(host));
            }
        }
        break;

    case CoinKey: /* --coin */
        break;

    case AccessLogFileKey: /* --access-log-file **/
        m_accessLog = arg;
        break;

    case VerboseKey: /* --verbose */
    case DebugKey:   /* --debug */
        return parseBoolean(key, true);

    case WorkersKey: /* --no-workers */
        return parseBoolean(key, false);

    case WorkersAdvKey:
        m_workersMode = Workers::parseMode(arg);
        break;

    case CustomDiffKey:   /* --custom-diff */
        return parseUint64(key, strtol(arg, nullptr, 10));

#   ifndef XMRIG_NO_TLS
    case TlsCertKey: /* --tls-cert */
        m_tls.setCert(arg);
        break;

    case TlsCertKeyKey: /* --tls-cert-key */
        m_tls.setKey(arg);
        break;

    case TlsDHparamKey: /* --tls-dhparam */
        m_tls.setDH(arg);
        break;

    case TlsCiphersKey: /* --tls-ciphers */
        m_tls.setCiphers(arg);
        break;

    case TlsCipherSuitesKey: /* --tls-ciphersuites */
        m_tls.setCipherSuites(arg);
        break;

    case TlsProtocolsKey: /* --tls-protocols */
        m_tls.setProtocols(arg);
        break;
#   endif

    default:
        break;
    }

    return true;
}


bool xmrig::Config::parseUint64(int key, uint64_t arg)
{
    if (!CommonConfig::parseUint64(key, arg)) {
        return false;
    }

    switch (key) {
    case CustomDiffKey: /* --custom-diff */
        if (arg >= 100 && arg < INT_MAX) {
            m_diff = arg;
        }
        break;

    case ReuseTimeoutKey: /* --reuse-timeout */
        m_reuseTimeout = static_cast<int>(arg);
        break;

    default:
        break;
    }

    return true;
}


void xmrig::Config::parseJSON(const rapidjson::Document &doc)
{
    CommonConfig::parseJSON(doc);

    const rapidjson::Value &bind = doc["bind"];
    if (bind.IsArray()) {
        for (const rapidjson::Value &value : bind.GetArray()) {
            if (value.IsObject()) {
                xmrig::BindHost host(value);
                if (host.isValid()) {
                    m_bind.push_back(std::move(host));
                }
            }
            else if (value.IsString()) {
                parseString(BindKey, value.GetString());
            }
        }
    }

#   ifndef XMRIG_NO_TLS
    const rapidjson::Value &tls = doc["tls"];
    if (tls.IsObject()) {
        m_tls = std::move(TlsConfig(tls));
    }
#   endif
}


void xmrig::Config::setMode(const char *mode)
{
    assert(mode != nullptr);
    if (mode == nullptr) {
        m_mode = NICEHASH_MODE;
        return;
    }

    constexpr const size_t size = sizeof(modeNames) / sizeof((modeNames)[0]);

    for (size_t i = 0; i < size; i++) {
        if (modeNames[i] && !strcmp(mode, modeNames[i])) {
            m_mode = (int) i;
            break;
        }
    }
}
