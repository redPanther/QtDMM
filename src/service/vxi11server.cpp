// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "service/vxi11server.h"

#include <QHostAddress>
#include <QTcpSocket>

namespace
{
// Does something accept connections on localhost:port?
bool portAnswers(quint16 port)
{
  QTcpSocket s;
  s.connectToHost(QHostAddress::LocalHost, port);
  return s.waitForConnected(300);
}
} // namespace

Vxi11Server::Vxi11Server(const ScpiEndpoint &endpoint, QObject *parent)
  : QObject(parent), m_core(endpoint), m_abort(&m_core), m_coreSrv(&m_coreDisp), m_abortSrv(&m_abortDisp),
    m_pmapTcp(&m_pmapDisp), m_pmapUdp(&m_pmapDisp)
{
  m_coreDisp.addService(&m_core);
  m_abortDisp.addService(&m_abort);
  m_pmapDisp.addService(&m_pmap);
}

Vxi11Server::~Vxi11Server()
{
  stop();
}

bool Vxi11Server::start(RpcbindMode mode, quint16 corePort, quint16 abortPort, quint16 portmapperPort)
{
  stop();
  m_error.clear();

  if (!m_coreSrv.listen(corePort))
  {
    m_error = QStringLiteral("VXI-11 core channel: %1").arg(m_coreSrv.errorString());
    stop();
    return false;
  }
  if (!m_abortSrv.listen(abortPort))
  {
    m_error = QStringLiteral("VXI-11 abort channel: %1").arg(m_abortSrv.errorString());
    stop();
    return false;
  }
  m_core.setAbortPort(m_abortSrv.port());
  m_regs = {{m_core.program(), m_core.version(), m_coreSrv.port()},
            {m_abort.program(), m_abort.version(), m_abortSrv.port()}};

  // A system rpcbind owns port 111, so an own portmapper cannot run next to it: if one answers
  // it is the only way, and a refused registration is an error.
  if (mode != RpcbindMode::No && m_rpcbind.isAvailable())
  {
    if (!registerWithRpcbind())
    {
      stop();
      return false;
    }
    m_systemRpcbind = true;
    qCDebug(lcVxi11) << "registered with the system rpcbind, core port" << m_coreSrv.port();
  }
  else if (mode == RpcbindMode::Yes)
  {
    m_error = QStringLiteral("no rpcbind reachable on 127.0.0.1:111");
    stop();
    return false;
  }
  else
  {
    m_pmap.clear();
    for (const Reg &reg : m_regs)
      m_pmap.registerProgram(reg.prog, reg.port);
    if (!m_pmapTcp.listen(portmapperPort) || !m_pmapUdp.bind(portmapperPort))
    {
      m_error = QStringLiteral("portmapper port %1 not available (%2); it is privileged on Linux/macOS")
                    .arg(portmapperPort)
                    .arg(m_pmapTcp.errorString());
      stop();
      return false;
    }
    qCDebug(lcVxi11) << "own portmapper on port" << portmapperPort << ", core port" << m_coreSrv.port();
  }

  m_running = true;
  return true;
}

// Registers core and abort with the system rpcbind. On failure m_error says why.
bool Vxi11Server::registerWithRpcbind()
{
  QList<Reg> done;
  for (const Reg &reg : m_regs)
  {
    bool ok = m_rpcbind.set(reg.prog, reg.vers, reg.port);
    if (!ok)
    {
      // An entry exists already: left behind by a crashed run, or served by another live process?
      const quint16 other = m_rpcbind.getPort(reg.prog, reg.vers);
      if (other != 0 && portAnswers(other))
      {
        m_error = QStringLiteral("VXI-11 is already served by another process (port %1); "
                                 "only one VXI-11 device per host is possible")
                      .arg(other);
      }
      else if (other != 0)
      {
        m_rpcbind.unset(reg.prog, reg.vers); // stale entry
        ok = m_rpcbind.set(reg.prog, reg.vers, reg.port);
      }
    }
    if (!ok)
    {
      if (m_error.isEmpty())
        m_error = QStringLiteral("rpcbind refused the registration (rights?)");
      for (const Reg &d : done)
        m_rpcbind.unset(d.prog, d.vers);
      return false;
    }
    done << reg;
  }
  return true;
}

void Vxi11Server::stop()
{
  if (m_systemRpcbind)
  {
    for (const Reg &reg : m_regs)
      m_rpcbind.unset(reg.prog, reg.vers);
    m_systemRpcbind = false;
  }
  m_pmapUdp.close();
  m_pmapTcp.close();
  m_abortSrv.close();
  m_coreSrv.close();
  m_core.reset();
  m_running = false;
}
