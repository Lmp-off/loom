#include "buffer.hpp"

// Явные инстанцирования очередей, используемых TDB.
template class RingBuffer<IncomingPacket>;
template class RingBuffer<OutgoingPacket>;
