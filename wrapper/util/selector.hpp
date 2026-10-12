#pragma once

#if defined FIFO_BLOCKFIFO
#include "wrapper/blockfifo.hpp"
#define FIFO wrapper::blockfifo::BlockFifo
#elif defined FIFO_MULTIFIFO
#include "wrapper/multififo.hpp"
#define FIFO wrapper::multififo::MultiFifo
#elif defined FIFO_KFIFO
#include "wrapper/kfifo.hpp"
#define FIFO wrapper::kfifo::KFifo
#elif defined FIFO_FAAAQUEUE
#include "wrapper/faaaqueue.hpp"
#define FIFO wrapper::faaaqueue::FAAAQueue
#elif defined FIFO_DCBO
#include "wrapper/dcbo.hpp"
#define FIFO wrapper::dcbo::DCBO
#elif defined FIFO_LCRQ
#include "wrapper/lcrq.hpp"
#define FIFO wrapper::lcrq::LCRQ
#elif defined FIFO_TBB_FIFO
#include "wrapper/tbb_fifo.hpp"
#define FIFO wrapper::tbb_fifo::TBBFifo
#elif defined FIFO_LOCKED_FIFO
#include "wrapper/locked_fifo.hpp"
#define FIFO wrapper::locked_fifo::LockedFifo
#else
#error No valid FIFO specified
#endif
