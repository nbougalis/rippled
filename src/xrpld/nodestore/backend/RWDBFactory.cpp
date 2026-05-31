#include <xrpld/nodestore/Factory.h>
#include <xrpld/nodestore/Manager.h>
#include <xrpld/nodestore/detail/DecodedBlob.h>
#include <xrpld/nodestore/detail/EncodedBlob.h>
#include <xrpld/nodestore/detail/codec.h>
#include <xrpl/basics/contract.h>
#include <boost/beast/core/string.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <memory>
#include <mutex>

namespace ripple {
namespace NodeStore {

class RWDBBackend : public Backend
{
private:
    std::string name_;
    beast::Journal journal_;
    bool isOpen_{false};

    struct base_uint_hasher
    {
        using result_type = std::size_t;

        result_type
        operator()(base_uint<256> const& value) const
        {
            return hardened_hash<>{}(value);
        }
    };

    using DataStore =
        std::map<uint256, std::vector<std::uint8_t>>;  // Store compressed blob
                                                       // data
    mutable std::recursive_mutex
        mutex_;  // Only needed for std::map implementation

    DataStore table_;

public:
    RWDBBackend(
        size_t keyBytes,
        Section const& keyValues,
        beast::Journal journal)
        : name_(get(keyValues, "path")), journal_(journal)
    {
        boost::ignore_unused(journal_);
        if (name_.empty())
            name_ = "node_db";
    }

    ~RWDBBackend() override
    {
        close();
    }

    std::string
    getName() override
    {
        return name_;
    }

    void
    open(bool createIfMissing) override
    {
        std::lock_guard lock(mutex_);
        if (isOpen_)
            Throw<std::runtime_error>("already open");
        isOpen_ = true;
    }

    bool
    isOpen() override
    {
        return isOpen_;
    }

    void
    close() override
    {
        std::lock_guard lock(mutex_);
        table_.clear();
        isOpen_ = false;
    }

    Status
    fetch(void const* key, boost::intrusive_ptr<NodeObject>* pObject) override
    {
        if (!isOpen_)
            return notFound;

        uint256 const hash(uint256::fromVoid(key));

        std::lock_guard lock(mutex_);
        auto it = table_.find(hash);
        if (it == table_.end())
            return notFound;

        scratch_buffer bf;
        auto const result =
            nodeobject_decompress(it->second.data(), it->second.size(), bf);
        *pObject = decodeNodeObject(hash.data(), result.first, result.second);
        return *pObject ? ok : dataCorrupt;
    }

    void
    store(boost::intrusive_ptr<NodeObject> const& object) override
    {
        if (!isOpen_)
            return;

        if (!object)
            return;

        EncodedBlob encoded(object);
        scratch_buffer bf;
        auto const result =
            nodeobject_compress(encoded.getData(), encoded.getSize(), bf);

        std::vector<std::uint8_t> compressed(
            static_cast<const std::uint8_t*>(result.first),
            static_cast<const std::uint8_t*>(result.first) + result.second);

        std::lock_guard lock(mutex_);
        table_[object->key()] = std::move(compressed);
    }

    void
    storeBatch(Batch const& batch) override
    {
        for (auto const& e : batch)
            store(e);
    }

    void
    sync() override
    {
    }

    void
    for_each(std::function<void(boost::intrusive_ptr<NodeObject>)> f) override
    {
        if (!isOpen_)
            return;

        std::lock_guard lock(mutex_);
        for (const auto& entry : table_)
        {
            scratch_buffer bf;
            auto const result = nodeobject_decompress(
                entry.second.data(), entry.second.size(), bf);
            if (auto decoded = decodeNodeObject(
                    entry.first.data(), result.first, result.second))
                f(decoded);
        }
    }

    int
    getWriteLoad() override
    {
        return 0;
    }

    void
    setDeletePath() override
    {
        close();
    }

    int
    fdRequired() const override
    {
        return 0;
    }

private:
    size_t
    size() const
    {
        std::lock_guard lock(mutex_);
        return table_.size();
    }
};

class RWDBFactory : public Factory
{
public:
    RWDBFactory()
    {
        Manager::instance().insert(*this);
    }

    ~RWDBFactory() override
    {
        Manager::instance().erase(*this);
    }

    std::string
    getName() const override
    {
        return "RWDB";
    }

    std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) override
    {
        return std::make_unique<RWDBBackend>(keyBytes, keyValues, journal);
    }
};

static RWDBFactory rwDBFactory;

}  // namespace NodeStore
}  // namespace ripple
