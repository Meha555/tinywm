#ifndef XCB_HPP
#define XCB_HPP

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

#include <string>
#include <unordered_map>

#include "utils.hpp"
#include <glog/logging.h>

namespace xcb
{

inline void errorHandler(xcb_generic_error_t *error, const char *message) noexcept
{
    if (error) {
        // fprintf(stderr, "ERROR: can't %s : %d\n", message, error->error_code);
        LOG(ERROR) << message << " failed. : " << error->error_code;
        free(error);
        error = nullptr;
    }
}

inline void errorHandler(xcb_void_cookie_t cookie, const char *message, xcb_connection_t *conn = xcb_connect(nullptr, nullptr)) noexcept
{
    if (auto error = xcb_request_check(conn, cookie)) {
        // fprintf(stderr, "ERROR: can't %s : %d\n", message, error->error_code);
        LOG(ERROR) << message << " failed. : " << error->error_code;
        free(error);
        error = nullptr;
    }
}

class ServerGraber
{
public:
    ServerGraber(xcb_connection_t *c = xcb_connect(nullptr, nullptr))
        : m_connection(c)
    {
        errorHandler(xcb_grab_server_checked(m_connection), "grab X Server", m_connection);
        xcb_flush(m_connection);
    }
    ~ServerGraber()
    {
        errorHandler(xcb_ungrab_server_checked(m_connection), "ungrab X Server", m_connection);
        xcb_flush(m_connection);
    }

private:
    xcb_connection_t *m_connection;
};

class Atom
{
public:
    explicit Atom(const std::string &name, bool only_if_exists = false, xcb_connection_t *c = xcb_connect(nullptr, nullptr))
        : m_connection(c)
        , m_retrieved(false)
        , m_cookie(xcb_intern_atom_unchecked(c, only_if_exists, name.length(), name.data()))
        , m_atom(XCB_ATOM_NONE)
        , m_name(name)
    {
    }
    ~Atom()
    {
        if (!m_retrieved && m_cookie.sequence) {
            xcb_discard_reply(m_connection, m_cookie.sequence);
        }
    }

    operator xcb_atom_t() const
    {
        (const_cast<Atom *>(this))->getReply();
        return m_atom;
    }
    bool isValid()
    {
        getReply();
        return m_atom != XCB_ATOM_NONE;
    }
    const std::string name() const
    {
        return m_name;
    }

private:
    void getReply()
    {
        if (m_retrieved || !m_cookie.sequence)
            return;
        utils::UniqueCPtr<xcb_intern_atom_reply_t> reply(xcb_intern_atom_reply(m_connection, m_cookie, nullptr));
        if (reply)
            m_atom = reply->atom;
        m_retrieved = true;
    }

    bool m_retrieved;
    std::string m_name;
    xcb_atom_t m_atom;
    xcb_intern_atom_cookie_t m_cookie;
    xcb_connection_t *m_connection;
};

class Atoms
{
    Atoms()
    {
        m_atoms.emplace("WM_PROTOCOLS", std::make_shared<Atom>("WM_PROTOCOLS"));
        m_atoms.emplace("WM_DELETE_WINDOW", std::make_shared<Atom>("WM_DELETE_WINDOW"));
    }
    Atoms(const Atoms &) = delete;
    std::unordered_map<std::string, std::shared_ptr<Atom>> m_atoms;

public:
    static inline Atoms *instance()
    {
        static Atoms atoms;
        return &atoms;
    }
    void insert(const std::string &name, bool only_if_exists = false, xcb_connection_t *c = xcb_connect(nullptr, nullptr))
    {
        if (!m_atoms.count(name))
            m_atoms.emplace(name, std::make_shared<Atom>(name, only_if_exists, c));
    }
    std::shared_ptr<Atom> atom(const std::string& name)
    {
        if (m_atoms.count(name))
            return m_atoms[name];
        return nullptr;
    }
};

} // namespace xcb

#endif // XCB_HPP