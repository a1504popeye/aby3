#include "LynxBinaryEngine.h"
#include <cryptoTools/Common/Matrix.h>
#include <cryptoTools/Common/Log.h>
#include <libOTe/Tools/Tools.h>
#include <cryptoTools/Crypto/sha1.h>
namespace Lynx
{

    using namespace oc;

    void BinaryEngine::setCir(BetaCircuit * cir, u64 width)
    {
        if (cir->mLevelCounts.size() == 0) cir->levelByAndDepth();

        //if (cir->mInputs.size() != 2) throw std::runtime_error(LOCATION);

        mCir = cir;
        //auto bits = sizeof(Word) * 8;
        //auto simdWidth = (width + bits - 1) / bits;

        // each row of mem corresponds to a wire. Each column of mem corresponds to 64 SIMD bits
        mMem.resize(width, mCir->mWireCount);
        //mMem[0].resize(cir->mWireCount, simdWidth);
        //mMem[1].resize(cir->mWireCount, simdWidth);
        //mMem[0].setZero();
        //mMem[1].setZero();


#ifdef BINARY_ENGINE_DEBUG
        if (mDebug)
        {
            mPlainWires_DEBUG.resize(width);
            for (auto& m : mPlainWires_DEBUG)
                m.resize(cir->mWireCount);
        }
#endif
    }

    void BinaryEngine::setReplicatedInput(u64 idx, const Matrix & in)
    {
        mLevel = 0;
        auto& inWires = mCir->mInputs[idx].mWires;
        auto simdWidth = mMem.simdWidth();
        auto inCols = in.cols();
        auto bits = sizeof(Word) * 8;

        if (mCir == nullptr)
            throw std::runtime_error(LOCATION);
        if (idx >= mCir->mInputs.size())
            throw std::invalid_argument("input index out of bounds");
        if (inCols != (inWires.size() + bits - 1) / bits)
            throw std::invalid_argument("input data wrong size");
        if (in.rows() != 1)
            throw std::invalid_argument("incorrect number of simd rows");


        std::array<char, 2> vals{ 0,~0 };

        for (u64 i = 0; i < 2; ++i)
        {
            auto& shares = mMem.mShares[i];
            oc::BitIterator iter((u8*)in.mShares[0].data(), 0);

            for (u64 j = 0; j < inWires.size(); ++j, ++iter)
            {
                if (shares.rows() >= inWires[j])
                    throw std::runtime_error(LOCATION);

                char v = vals[*iter];
                memset(shares.data() + simdWidth * inWires[j], v, simdWidth * sizeof(Word));
            }
        }
    }

    void BinaryEngine::setInput(u64 idx, const Matrix & in)
    {
        mLevel = 0;
        auto& inWires = mCir->mInputs[idx].mWires;
        auto simdWidth = mMem.simdWidth();
        auto bits = sizeof(Word) * 8;

        auto inCols = in.cols();
        auto inRows = in.rows();
        auto inSimdWidth = (inRows + bits - 1) / bits;

        if (mCir == nullptr)
            throw std::runtime_error(LOCATION);
        if (idx >= mCir->mInputs.size())
            throw std::invalid_argument("input index out of bounds");
        if (inCols != (inWires.size() + bits - 1) / bits)
            throw std::invalid_argument("input data wrong size");
        if (simdWidth < inSimdWidth)
            throw std::invalid_argument("incorrect number of simd rows");

        for (u64 i = 0; i < inWires.size() - 1; ++i)
        {
            if (inWires[i] + 1 != inWires[i + 1])
                throw std::runtime_error("expecting contiguous input wires. " LOCATION);
        }

        for (u64 i = 0; i < 2; ++i)
        {
            auto& shares = mMem.mShares[i];

            MatrixView<u8> inView(
                (u8*)(in.mShares[i].data()),
                (u8*)(in.mShares[i].data() + in.mShares[i].size()),
                sizeof(Word));

            if (inWires.back() > shares.rows())
                throw std::runtime_error(LOCATION);

            MatrixView<u8> memView(
                (u8*)(shares.data() + simdWidth * inWires.front()),
                (u8*)(shares.data() + simdWidth * (inWires.back() + 1)),
                sizeof(Word) * simdWidth);

            if (memView.data() + memView.size() > (u8*)(shares.data() + shares.size()))
                throw std::runtime_error(LOCATION);

            oc::sse_transpose(inView, memView);
            //std::cout << " in* " << std::endl;
            //for (u64 r = 0; r < inView.bounds()[0]; ++r)
            //{
            //	BitVector bv(inView[r].data(), inView[r].size() * 8);
            //	std::cout << bv;
            //	//for (u64 c = 0; c < inView.bounds()[1]; ++c)
            //	//{
            //	//	std::cout << std::hex << (u64)inView(r, c) << " ";
            //	//}
            //	std::cout << std::endl;
            //}
            //std::cout << std::endl;
            //
            //
            //std::cout << " out*" << std::endl;
            //for (u64 r = 0; r < memView.bounds()[0]; ++r)
            //{
            //	for (u64 c = 0; c < memView.bounds()[1]; ++c)
            //	{
            //		std::cout << std::hex << (u64)memView(r, c) << " ";
            //	}
            //	std::cout << std::endl;
            //}
            //std::cout << std::endl;
        }

#ifdef BINARY_ENGINE_DEBUG

        for (u64 r = 0; r < mPlainWires_DEBUG.size(); ++r)
        {
            auto& m = mPlainWires_DEBUG[r];
            auto bv0 = BitVector((u8*)in.mShares[0].row(r).data(), inWires.size());
            auto bv1 = BitVector((u8*)in.mShares[1].row(r).data(), inWires.size());

            for (u64 i = 0; i < inWires.size(); ++i)
            {
                m[inWires[i]].mBits[mPartyIdx] = bv0[i];
                m[inWires[i]].mBits[mPrevIdx] = bv1[i];

                //if (inWires[i] < 10)
                //	ostreamLock(std::cout) << mPartyIdx << " w[" << inWires[i] << "] = "
                //	<< (int)m[inWires[i]].mBits[mPartyIdx] << std::endl;
            }
        }

        //Matrix nn(in);
        //getOutput(inWires, nn);

        //if (nn.mShares[0] != in.mShares[0])
        //{
        //	std::cout << "exp " << in.mShares[0] << std::endl;
        //	std::cout << "act " << nn.mShares[0] << std::endl;
        //	throw std::runtime_error(LOCATION);
        //}
        //if (nn.mShares[1] != in.mShares[1]) throw std::runtime_error(LOCATION);
#endif

    }

    void BinaryEngine::setInput(u64 idx, const PackedBinMatrix & in)
    {
        auto simdWidth = mMem.simdWidth();

        if (mCir == nullptr)
            throw std::runtime_error(LOCATION);
        if (idx >= mCir->mInputs.size())
            throw std::invalid_argument("input index out of bounds");
        if (in.shareCount() != mMem.shareCount())
            throw std::runtime_error(LOCATION);
        if (in.bitCount() != mCir->mInputs[idx].mWires.size())
            throw std::runtime_error(LOCATION);

        for (u64 i = 0; i < 2; ++i)
        {
            auto& share = mMem.mShares[i];
            auto& wires = mCir->mInputs[idx];

            for (u64 j = 0; j < wires.size(); ++j)
            {
                memcpy(share.data() + simdWidth * wires[j], in.mShares[i].data() + simdWidth * j, simdWidth * sizeof(Word));
            }
        }
    }

    void BinaryEngine::evaluate()
    {
#ifdef BINARY_ENGINE_DEBUG

        if (mDebug)
        {
            for (u64 r = 0; r < mPlainWires_DEBUG.size(); ++r)
            {
                auto& m = mPlainWires_DEBUG[r];

                std::vector<u8> s0(m.size()), s1;
                for (u64 i = 0; i < s0.size(); ++i)
                {
                    s0[i] = m[i].mBits[mPartyIdx];
                }
                mPrev.asyncSendCopy(s0);
                mNext.asyncSendCopy(s0);
                mPrev.recv(s0);
                mNext.recv(s1);
                if (s0.size() != m.size())
                    throw std::runtime_error(LOCATION);

                for (u64 i = 0; i < m.size(); ++i)
                {
                    if (m[i].mBits[mPrevIdx] != s0[i])
                        throw std::runtime_error(LOCATION);

                    m[i].mBits[mNextIdx] = s1[i];
                }
            }
        }
#endif

        while (hasMoreRounds())
        {
            evaluateRound();
        }
    }

    void BinaryEngine::evaluateRound()
    {

        auto simdWidth = mMem.simdWidth();

        if (mLevel > mCir->mLevelCounts.size())
        {
            throw std::runtime_error("evaluateRound() was called but no rounds remain... " LOCATION);
        }
        if (mCir->mLevelCounts.size() == 0 && mCir->mNonlinearGateCount)
            throw std::runtime_error("the level by and gate function must be called first." LOCATION);

        if (mLevel)
        {
            if (mUpdateLocations.size())
            {
                // recv the data for the previous level
                std::vector<Word> updates(mUpdateLocations.size() * simdWidth);
                auto iter = updates.begin();


                mPrev.recv(updates);
                iter = updates.begin();

                for (u64 j = 0; j < mUpdateLocations.size(); ++j)
                {
                    auto out = mUpdateLocations[j];
                    memcpy(&mMem.mShares[1](out), &*iter, simdWidth * sizeof(Word));
                    iter += simdWidth;
                }
            }
        }
        else {
            mGateIter = mCir->mGates.data();
        }
        Word ccWord;
        memset(&ccWord, 0xCC, sizeof(Word));

        Word allOnesIfP0 = 0;

        auto& shares = mMem.mShares;

        if (mLevel < mCir->mLevelCounts.size())
        {
            auto andGateCount = mCir->mLevelAndCounts[mLevel];
            auto gateCount = mCir->mLevelCounts[mLevel];

            mUpdateLocations.resize(andGateCount);
            auto updateIter = mUpdateLocations.data();
            std::vector<Word> updates(andGateCount * simdWidth);
            auto iter = updates.begin();

            for (u64 j = 0; j < gateCount; ++j, ++mGateIter)
            {
                const auto& gate = *mGateIter;
                const auto& type = gate.mType;
                auto in0 = gate.mInput[0] * simdWidth;
                auto in1 = gate.mInput[1] * simdWidth;
                auto out = gate.mOutput * simdWidth;

                //#ifdef BINARY_ENGINE_DEBUG
                //			{
                //				std::vector<Word> in0Share, in1Share;
                //				mPrev.asyncSendCopy(&shares[0](in0), simdWidth);
                //				mPrev.asyncSendCopy(&shares[0](in1), simdWidth);
                //				mNext.recv(in0Share);
                //				mNext.recv(in1Share);
                //
                //				for (u64 k = 0; k < simdWidth; ++k)
                //				{
                //					in0Share[k] ^= shares[0](in0 + k) ^ shares[1](in0 + k);
                //					in1Share[k] ^= shares[0](in1 + k) ^ shares[1](in1 + k);
                //				}
                //
                //				for (u64 r = 0; r < mPlainWires_DEBUG.size(); ++r)
                //				{
                //					auto& m = mPlainWires_DEBUG[r];
                //
                //					auto k = r / (sizeof(Word) * 8);
                //					auto j = r % (sizeof(Word) * 8);
                //					auto mem0 = (s0[k] >> j) & 1;
                //				}
                //
                //			}
                //#endif

                switch (gate.mType)
                {
                case GateType::Xor:
                    for (u64 k = 0; k < simdWidth; ++k)
                    {
                        shares[0](out) = shares[0](in0) ^ shares[0](in1);
                        shares[1](out) = shares[1](in0) ^ shares[1](in1);

                        ++in0;
                        ++in1;
                        ++out;
                    }
                    break;
                case GateType::And:
                    *updateIter++ = out;
                    for (u64 k = 0; k < simdWidth; ++k)
                    {

                        TODO("add the randomization back");
                        *iter = shares[0](out)
                            = (shares[0](in0) & shares[0](in1))
                            ^ (shares[0](in0) & shares[1](in1))
                            ^ (shares[1](in0) & shares[0](in1))
                            /*^ getBinaryShare()*/;

#ifndef NDEBUG
                        if (shares[1](in0) == ccWord || shares[1](in1) == ccWord)
                            throw std::runtime_error(LOCATION);
                        shares[1](out) = ccWord;
#endif
                        ++iter;
                        ++in0;
                        ++in1;
                        ++out;
                    }
                    break;
                case GateType::Nor:
                    *updateIter++ = out;
                    for (u64 k = 0; k < simdWidth; ++k)
                    {
                        auto mem00 = shares[0](in0) ^ -1;
                        auto mem01 = shares[0](in1) ^ -1;
                        auto mem10 = shares[1](in0) ^ -1;
                        auto mem11 = shares[1](in1) ^ -1;

                        TODO("add the randomization back");
                        *iter = shares[0](out)
                            = (mem00 & mem01)
                            ^ (mem00 & mem11)
                            ^ (mem10 & mem01)
                            /*^ getBinaryShare()*/;

#ifndef NDEBUG
                        if (shares[1](in0) == ccWord || shares[1](in1) == ccWord)
                            throw std::runtime_error(LOCATION);
                        shares[1](out) = ccWord;
#endif
                        ++iter;
                        ++in0;
                        ++in1;
                        ++out;
                    }
                    break;
                case GateType::Nxor:
                    for (u64 k = 0; k < simdWidth; ++k)
                    {
                        shares[0](out) = ~(shares[0](in0) ^ shares[0](in1));
                        shares[1](out) = ~(shares[1](in0) ^ shares[1](in1));

                        ++in0;
                        ++in1;
                        ++out;
                    }
                    break;
                case GateType::a:
                    //memcpy(&shares[0](out), &shares[0](in0), simdWidth * sizeof(Word));
                    //memcpy(&shares[1](out), &shares[1](in0), simdWidth * sizeof(Word));
                    for (u64 k = 0; k < simdWidth; ++k)
                    {
                        if (shares[1](in0) == ccWord)
                            throw std::runtime_error(LOCATION);

                        shares[0](out) = shares[0](in0);
                        shares[1](out) = shares[1](in0);

                        ++in0;
                        ++in1;
                        ++out;
                    }
                    break;
                case GateType::na_And:
                    *updateIter++ = out;
                    for (u64 k = 0; k < simdWidth; ++k)
                    {

                        TODO("add the randomization back");
                        *iter = shares[0](out)
                            = ((~shares[0](in0)) & shares[0](in1))
                            ^ ((~shares[0](in0)) & shares[1](in1))
                            ^ ((~shares[1](in0)) & shares[0](in1))
                            /*^ getBinaryShare()*/;

#ifndef NDEBUG
                        if (shares[1](in0) == ccWord || shares[1](in1) == ccWord)
                            throw std::runtime_error(LOCATION);
                        shares[1](out) = ccWord;
#endif
                        ++iter;
                        ++in0;
                        ++in1;
                        ++out;
                    }
                    break;
                case GateType::Zero:
                case GateType::nb_And:
                case GateType::nb:
                case GateType::na:
                case GateType::Nand:
                case GateType::nb_Or:
                case GateType::b:
                case GateType::na_Or:
                case GateType::Or:
                case GateType::One:
                default:

                    throw std::runtime_error("BinaryEngine unsupported GateType " LOCATION);
                    break;
                }



#ifdef BINARY_ENGINE_DEBUG
                if (mDebug)
                {

                    out = gate.mOutput * simdWidth;

                    mNext.asyncSendCopy(&shares[0](out), simdWidth);
                    mPrev.asyncSendCopy(&shares[0](out), simdWidth);

                    std::vector<Word> s0(simdWidth), s1(simdWidth);
                    mPrev.recv(s0);
                    mNext.recv(s1);

                    for (u64 k = 0; k < simdWidth; ++k)
                        s0[k] ^= s1[k] ^ shares[0](out + k);

                    for (u64 r = 0; r < mPlainWires_DEBUG.size(); ++r)
                    {
                        auto k = r / (sizeof(Word) * 8);
                        auto j = r % (sizeof(Word) * 8);
                        Word plain = (s0[k] >> j) & 1;
                        Word zeroShare = (shares[0](out + k) >> j) & 1;

                        auto gIn0 = gate.mInput[0];
                        auto gIn1 = gate.mInput[1];
                        auto gOut = gate.mOutput;

                        auto& m = mPlainWires_DEBUG[r];
                        auto gIdx = mGateIter - mCir->mGates.data();

                        m[gOut].assign(m[gIn0], m[gIn1], gate.mType);

                        if (zeroShare != m[gOut].mBits[mPartyIdx])
                            throw std::runtime_error(LOCATION);


                        if (plain != m[gOut].val())
                        {
                            ostreamLock(std::cout)
                                << "\n g" << gIdx << " act: " << plain << "   exp: " << (int)m[gate.mOutput].val() << std::endl
                                << m[gIn0].val() << " " << gateToString(type) << " " << m[gIn1].val() << " -> " << m[gOut].val() << std::endl
                                << gIn0 << " " << gateToString(type) << " " << gIn1 << " -> " << int(gOut) << std::endl;
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            throw std::runtime_error(LOCATION);
                        }

                    }
                }
#endif
            }

            if (iter != updates.end()) throw std::runtime_error(LOCATION);

            if (updates.size())
                mNext.asyncSend(std::move(updates));
        }
        mLevel++;
        if (mGateIter > mCir->mGates.data() + mCir->mGates.size()) throw std::runtime_error(LOCATION);
    }

    void BinaryEngine::getOutput(u64 i, Matrix & out)
    {
        if (mCir->mOutputs.size() <= i) throw std::runtime_error(LOCATION);

        const auto& outWires = mCir->mOutputs[i].mWires;

        getOutput(outWires, out);
    }

    void BinaryEngine::getOutput(u64 idx, PackedBinMatrix & out)
    {
        auto bits = sizeof(Word) * 8;
        const auto& outWires = mCir->mOutputs[idx].mWires;

        out.resize(mMem.shareCount(), outWires.size());
        auto simdWidth = mMem.simdWidth();

        for (u64 j = 0; j < 2; ++j)
        {
            auto jj = !j ? mPartyIdx : mPrevIdx;
            auto dest = out.mShares[j].data();

            for (u64 wireIdx = 0; wireIdx < outWires.size(); ++wireIdx)
            {
                //if (mCir->isInvert(outWires[wireIdx]))
                //	throw std::runtime_error(LOCATION);
                auto wire = outWires[wireIdx];

                auto md = mMem.mShares[j].data();
                auto ms = mMem.mShares[j].size();
                auto src = md + wire * simdWidth;
                auto size = simdWidth * sizeof(Word);

                //if (src + simdWidth > md + ms)
                //    throw std::runtime_error(LOCATION);
                //if (dest + simdWidth > out..data() + outMem.size())
                //    throw std::runtime_error(LOCATION);


                memcpy(dest, src, size);

                // check if we need to ivert these output wires.
                if (mCir->isInvert(wire))
                {
                    for (u64 k = 0; k < simdWidth; ++k)
                    {
                        dest[k] = ~dest[k];
                    }
                }

                dest += simdWidth;

#ifdef BINARY_ENGINE_DEBUG
                for (u64 r = 0; r < mPlainWires_DEBUG.size(); ++r)
                {
                    auto m = mPlainWires_DEBUG[r];
                    auto k = r / (sizeof(Word) * 8);
                    auto l = r % (sizeof(Word) * 8);
                    Word plain = (src[k] >> l) & 1;

                    if (m[wire].mBits[jj] != plain)
                        throw std::runtime_error(LOCATION);
                }
#endif
            }
        }
    }

    void BinaryEngine::getOutput(const std::vector<BetaWire>& outWires, Matrix & out)
    {
        auto bits = sizeof(Word) * 8;
        if (outWires.size() > out.cols() * bits) throw std::runtime_error(LOCATION);
        //auto outCols = roundUpTo(outWires.size(), 8);

        auto simdWidth = mMem.simdWidth();
        Eigen::Matrix<Word, Eigen::Dynamic, Eigen::Dynamic> outMem;
        outMem.resize(outWires.size(), simdWidth);

        for (u64 j = 0; j < 2; ++j)
        {
            auto jj = !j ? mPartyIdx : mPrevIdx;
            auto dest = outMem.data();

            for (u64 i = 0; i < outWires.size(); ++i)
            {
                //if (mCir->isInvert(outWires[i]))
                //	throw std::runtime_error(LOCATION);

                auto md = mMem.mShares[j].data();
                auto ms = mMem.mShares[j].size();
                auto src = md + outWires[i] * simdWidth;
                auto size = simdWidth * sizeof(Word);

                if (src + simdWidth > md + ms)
                    throw std::runtime_error(LOCATION);
                if (dest + simdWidth > outMem.data() + outMem.size())
                    throw std::runtime_error(LOCATION);


                memcpy(dest, src, size);

                // check if we need to ivert these output wires.
                if (mCir->isInvert(outWires[i]))
                {
                    for (u64 k = 0; k < simdWidth; ++k)
                    {
                        dest[k] = ~dest[k];
                    }
                }

                dest += simdWidth;

#ifdef BINARY_ENGINE_DEBUG
                for (u64 r = 0; r < mPlainWires_DEBUG.size(); ++r)
                {
                    auto m = mPlainWires_DEBUG[r];
                    auto k = r / (sizeof(Word) * 8);
                    auto l = r % (sizeof(Word) * 8);
                    Word plain = (src[k] >> l) & 1;

                    if (m[outWires[i]].mBits[jj] != plain)
                        throw std::runtime_error(LOCATION);
                }
#endif
            }
            MatrixView<u8> in((u8*)outMem.data(), (u8*)(outMem.data() + outMem.size()), simdWidth * sizeof(Word));
            MatrixView<u8> oout((u8*)out.mShares[j].data(), (u8*)(out.mShares[j].data() + out.mShares[j].size()), sizeof(Word) * out.mShares[j].cols());
            //memset(oout.data(), 0, oout.size());
            out.mShares[j].setZero();

            sse_transpose(in, oout);

#ifdef BINARY_ENGINE_DEBUG
            if (mDebug)
            {

                for (u64 i = 0; i < outWires.size(); ++i)
                {
                    for (u64 r = 0; r < mPlainWires_DEBUG.size(); ++r)
                    {
                        auto m = mPlainWires_DEBUG[r];
                        auto k = i / (sizeof(Word) * 8);
                        auto l = i % (sizeof(Word) * 8);
                        auto outWord = out.mShares[j](r, k);
                        auto plain = (outWord >> l) & 1;

                        auto inv = mCir->isInvert(outWires[i]) & 1;
                        auto xx = m[outWires[i]].mBits[jj] ^ inv;
                        if (xx != plain)
                            throw std::runtime_error(LOCATION);
                    }
                }
                auto mod = (outWires.size() % 64);
                u64 mask = ~(mod ? (Word(1) << mod) - 1 : -1);
                for (u64 i = 0; i < out.rows(); ++i)
                {
                    auto cols = out.mShares[j].cols();
                    if (out.mShares[j].row(i)[cols - 1] & mask)
                        throw std::runtime_error(LOCATION);
                }
            }
#endif
        }
    }


#ifdef BINARY_ENGINE_DEBUG

    void BinaryEngine::DEBUG_Triple::assign(
        const DEBUG_Triple & in0,
        const DEBUG_Triple & in1,
        GateType type)
    {
        auto vIn0 = int(in0.val());
        auto vIn1 = int(in1.val());

        if (vIn0 > 1) throw std::runtime_error(LOCATION);
        if (vIn1 > 1) throw std::runtime_error(LOCATION);

        u8 plain;
        if (type == GateType::Xor)
        {
            plain = vIn0 ^ vIn1;
            std::array<u8, 3> t;

            t[0] = in0.mBits[0] ^ in1.mBits[0];
            t[1] = in0.mBits[1] ^ in1.mBits[1];
            t[2] = in0.mBits[2] ^ in1.mBits[2];

            mBits = t;
        }
        else if (type == GateType::Nxor)
        {
            plain = vIn0 ^ vIn1 ^ 1;

            std::array<u8, 3> t;

            t[0] = in0.mBits[0] ^ in1.mBits[0] ^ 1;
            t[1] = in0.mBits[1] ^ in1.mBits[1] ^ 1;
            t[2] = in0.mBits[2] ^ in1.mBits[2] ^ 1;

            mBits = t;
        }
        else if (type == GateType::And)
        {
            plain = vIn0 & vIn1;
            std::array<u8, 3> t;
            for (u64 b = 0; b < 3; ++b)
            {

                auto bb = b ? (b - 1) : 2;

                if (bb != (b + 2) % 3) throw std::runtime_error(LOCATION);

                auto in00 = in0.mBits[b];
                auto in01 = in0.mBits[bb];
                auto in10 = in1.mBits[b];
                auto in11 = in1.mBits[bb];

                t[b]
                    = in00 & in10
                    ^ in00 & in11
                    ^ in01 & in10;
            }

            mBits = t;
        }

        else if (type == GateType::na_And)
        {
            plain = (1 ^ vIn0) & vIn1;
            std::array<u8, 3> t;
            for (u64 b = 0; b < 3; ++b)
            {

                auto bb = b ? (b - 1) : 2;

                if (bb != (b + 2) % 3) throw std::runtime_error(LOCATION);

                auto in00 = 1 ^ in0.mBits[b];
                auto in01 = 1 ^ in0.mBits[bb];
                auto in10 = in1.mBits[b];
                auto in11 = in1.mBits[bb];

                t[b]
                    = in00 & in10
                    ^ in00 & in11
                    ^ in01 & in10;
            }

            mBits = t;
        }
        else if (type == GateType::Nor)
        {
            plain = (vIn0 | vIn1) ^ 1;


            std::array<u8, 3> t;
            for (u64 b = 0; b < 3; ++b)
            {
                auto bb = b ? (b - 1) : 2;
                auto in00 = 1 ^ in0.mBits[b];
                auto in01 = 1 ^ in0.mBits[bb];
                auto in10 = 1 ^ in1.mBits[b];
                auto in11 = 1 ^ in1.mBits[bb];

                t[b]
                    = in00 & in10
                    ^ in00 & in11
                    ^ in01 & in10;
            }

            mBits = t;
        }
        else if (type == GateType::a)
        {
            plain = vIn0;
            mBits = in0.mBits;
        }
        else
            throw std::runtime_error(LOCATION);

        if (plain != (mBits[0] ^ mBits[1] ^ mBits[2]))
        {
            //ostreamLock(std::cout) /*<< " g" << (mGateIter - mCir->mGates.data())
            //<< " act: " << plain << "   exp: " << (int)m[gate.mOutput] << std::endl*/

            //	<< vIn0 << " " << gateToString(type) << " " << vIn1 << " -> " << int(m[gOut]) << std::endl
            //	<< gIn0 << " " << gateToString(type) << " " << gIn1 << " -> " << int(gOut) << std::endl;
            //std::this_thread::sleep_for(std::chrono::milliseconds(100));
            throw std::runtime_error(LOCATION);
        }
    }
#endif
}