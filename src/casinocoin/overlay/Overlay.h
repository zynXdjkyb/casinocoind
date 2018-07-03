//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

//==============================================================================
/*
    2017-06-30  ajochems        Refactored for casinocoin
*/
//==============================================================================

#ifndef CASINOCOIN_OVERLAY_OVERLAY_H_INCLUDED
#define CASINOCOIN_OVERLAY_OVERLAY_H_INCLUDED

#include <casinocoin/json/json_value.h>
#include <casinocoin/overlay/Peer.h>
#include <casinocoin/overlay/PeerSet.h>
#include <casinocoin/server/Handoff.h>
#include <casinocoin/beast/asio/ssl_bundle.h>
#include <beast/http/message.hpp>
#include <casinocoin/core/Stoppable.h>
#include <casinocoin/core/DeadlineTimer.h>
#include <casinocoin/beast/utility/PropertyStream.h>
#include <casinocoin/protocol/Protocol.h>
#include <memory>
#include <type_traits>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <functional>

namespace boost { namespace asio { namespace ssl { class context; } } }

namespace casinocoin {

class TMDFSReportStateData;

/** Manages the set of connected peers. */
class Overlay
    : public Stoppable
    , public beast::PropertyStream::Source
{
protected:
    // VFALCO NOTE The requirement of this constructor is an
    //             unfortunate problem with the API for
    //             Stoppable and PropertyStream
    //
    Overlay (Stoppable& parent)
        : Stoppable ("Overlay", parent)
        , beast::PropertyStream::Source ("peers")
    {
    }

public:
    enum class Promote
    {
        automatic,
        never,
        always
    };

    struct Setup
    {
        std::shared_ptr<boost::asio::ssl::context> context;
        bool expire = false;
        beast::IP::Address public_ip;
        int ipLimit = 0;
    };

    using PeerSequence = std::vector <std::shared_ptr<Peer>>;

    virtual ~Overlay() = default;

    /** Conditionally accept an incoming HTTP request. */
    virtual
    Handoff
    onHandoff (std::unique_ptr <beast::asio::ssl_bundle>&& bundle,
        http_request_type&& request,
            boost::asio::ip::tcp::endpoint remote_address) = 0;

    /** Establish a peer connection to the specified endpoint.
        The call returns immediately, the connection attempt is
        performed asynchronously.
    */
    virtual
    void
    connect (beast::IP::Endpoint const& address) = 0;

    /** Returns the maximum number of peers we are configured to allow. */
    virtual
    int
    limit () = 0;

    /** Returns the number of active peers.
        Active peers are only those peers that have completed the
        handshake and are using the peer protocol.
    */
    virtual
    std::size_t
    size () = 0;

    /** Returns information reported to the crawl cgi command. */
    virtual
    Json::Value
    crawl() = 0;

    /** Return diagnostics on the status of all peers.
        @deprecated This is superceded by PropertyStream
    */
    virtual
    Json::Value
    json () = 0;

    /** Returns a sequence representing the current list of peers.
        The snapshot is made at the time of the call.
    */
    virtual
    PeerSequence
    getActivePeers () = 0;

    /** Returns a sequence representing the current list of Sane peers.
        The snapshot is made at the time of the call.
    */
    virtual
    PeerSequence
    getSanePeers () = 0;

    /** Calls the checkSanity function on each peer
        @param index the value to pass to the peer's checkSanity function
    */
    virtual
    void
    checkSanity (std::uint32_t index) = 0;

    /** Calls the check function on each peer
    */
    virtual
    void
    check () = 0;

    /** Returns the peer with the matching short id, or null. */
    virtual
    std::shared_ptr<Peer>
    findPeerByShortID (Peer::id_t const& id) = 0;

    /** Broadcast a proposal. */
    virtual
    void
    send (protocol::TMProposeSet& m) = 0;

    /** Broadcast a validation. */
    virtual
    void
    send (protocol::TMValidation& m) = 0;

    /** Relay a proposal. */
    virtual
    void
    relay (protocol::TMProposeSet& m,
        uint256 const& uid) = 0;

    /** Relay a validation. */
    virtual
    void
    relay (protocol::TMValidation& m,
        uint256 const& uid) = 0;

    virtual
    TMDFSReportStateData&
    getDFSReportStateData() = 0;

    virtual
    void
    startDFSReportStateCrawl(LedgerIndex const& startLedger) = 0;

    /** Visit every active peer and return a value
        The functor must:
        - Be callable as:
            void operator()(std::shared_ptr<Peer> const& peer);
         - Must have the following type alias:
            using return_type = void;
         - Be callable as:
            Function::return_type operator()() const;

        @param f the functor to call with every peer
        @returns `f()`

        @note The functor is passed by value!
    */
    template <typename UnaryFunc>
    std::enable_if_t<! std::is_void<
            typename UnaryFunc::return_type>::value,
                typename UnaryFunc::return_type>
    foreach (UnaryFunc f)
    {
        for (auto const& p : getActivePeers())
            f (p);
        return f();
    }

    /** Visit every active peer
        The visitor functor must:
         - Be callable as:
            void operator()(std::shared_ptr<Peer> const& peer);
         - Must have the following type alias:
            using return_type = void;

        @param f the functor to call with every peer
    */
    template <class Function>
    std::enable_if_t <
        std::is_void <typename Function::return_type>::value,
        typename Function::return_type
    >
    foreach(Function f)
    {
        for (auto const& p : getActivePeers())
            f (p);
    }

    /** Select from active peers

        Scores all active peers.
        Tries to accept the highest scoring peers, up to the requested count,
        Returns the number of selected peers accepted.

        The score function must:
        - Be callable as:
           bool (PeerImp::ptr)
        - Return a true if the peer is prefered

        The accept function must:
        - Be callable as:
           bool (PeerImp::ptr)
        - Return a true if the peer is accepted

    */
    virtual
    std::size_t
    selectPeers (PeerSet& set, std::size_t limit, std::function<
        bool(std::shared_ptr<Peer> const&)> score) = 0;
};

struct ScoreHasLedger
{
    uint256 const& hash_;
    std::uint32_t seq_;
    bool operator()(std::shared_ptr<Peer> const&) const;

    ScoreHasLedger (uint256 const& hash, std::uint32_t seq)
        : hash_ (hash), seq_ (seq)
    {}
};

struct ScoreHasTxSet
{
    uint256 const& hash_;
    bool operator()(std::shared_ptr<Peer> const&) const;

    ScoreHasTxSet (uint256 const& hash) : hash_ (hash)
    {}
};

}

#endif
