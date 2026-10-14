/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include "samp-amx.hpp"
#include "samp-plugin.hpp"
#include <Impl/pool_impl.hpp>
#include <Server/Components/Pawn/pawn.hpp>
#include <sdk.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class SampPawnScript final : public IPawnScript
{
private:
	AMX* amx_ = nullptr;
	int id_ = -1;
	bool loaded_ = false;

	static int unsupported() noexcept
	{
		return AMX_ERR_NOTFOUND;
	}

public:
	SampPawnScript(AMX* amx, int id) noexcept
		: amx_(amx)
		, id_(id)
		, loaded_(amx != nullptr)
	{
	}

	using IPawnScript::Register;

	int Allot(int, cell*, cell**) override { return unsupported(); }
	int Callback(cell, cell*, const cell*) override { return unsupported(); }
	int Cleanup() override { return unsupported(); }
	int Clone(AMX*, void*) const override { return unsupported(); }
	int FindNative(const char*, int*) const override { return unsupported(); }
	int FindPubVar(const char*, cell*) const override { return unsupported(); }
	int FindTagId(cell, char*) const override { return unsupported(); }
	int Flags(uint16_t*) const override { return unsupported(); }
	int GetNative(int, char*) const override { return unsupported(); }
	int GetNativeByIndex(int, AMX_NATIVE_INFO*) const override { return unsupported(); }
	int GetPublic(int, char*) const override { return unsupported(); }
	int GetPubVar(int, char*, cell*) const override { return unsupported(); }
	int GetTag(int, char*, cell*) const override { return unsupported(); }
	int GetUserData(long, void**) const override { return unsupported(); }
	int Init(void*) override { return unsupported(); }
	int InitJIT(void*, void*) override { return unsupported(); }
	int MemInfo(long*, long*, long*) const override { return unsupported(); }
	int NameLength(int*) const override { return unsupported(); }
	AMX_NATIVE_INFO* NativeInfo(const char*, AMX_NATIVE) const override { return nullptr; }
	int NumNatives(int*) const override { return unsupported(); }
	int NumPublics(int*) const override { return unsupported(); }
	int NumPubVars(int*) const override { return unsupported(); }
	int NumTags(int*) const override { return unsupported(); }
	int RaiseError(int) override { return unsupported(); }
	int SetCallback(AMX_CALLBACK) override { return unsupported(); }
	int SetDebugHook(AMX_DEBUG) override { return unsupported(); }
	int SetUserData(long, void*) override { return unsupported(); }
	int StrSize(const cell*, int*) const override { return unsupported(); }
	int UTF8Check(const char*, int*) const override { return unsupported(); }
	int UTF8Get(const char*, const char**, cell*) const override { return unsupported(); }
	int UTF8Len(const cell*, int*) const override { return unsupported(); }
	int UTF8Put(char*, char**, int, cell) const override { return unsupported(); }
	int CallNativeArray(const char*, Span<Impl::NativeParam>) override { return unsupported(); }

	int Exec(cell* result, int index) override
	{
		return SampAmx::exec(amx_, result, index);
	}

	int FindPublic(const char* name, int* index) const override
	{
		return SampAmx::findPublic(amx_, name, index);
	}

	int GetAddr(cell address, cell** physicalAddress) const override
	{
		return SampAmx::getAddr(amx_, address, physicalAddress);
	}

	int GetString(char const* destination, const cell* source, bool useWchar, size_t size) const override
	{
		return SampAmx::getString(const_cast<char*>(destination), source, useWchar ? 1 : 0, size);
	}

	int GetString(char* destination, const cell* source, bool useWchar, size_t size) override
	{
		return SampAmx::getString(destination, source, useWchar ? 1 : 0, size);
	}

	int MakeAddr(cell* physicalAddress, cell* amxAddress) const override
	{
		if (!amx_ || !amx_->base || !physicalAddress || !amxAddress)
		{
			return AMX_ERR_MEMACCESS;
		}

		const auto* header = reinterpret_cast<const AMX_HEADER*>(amx_->base);
		const auto* begin = amx_->data ? amx_->data : amx_->base + header->dat;
		const auto beginAddress = reinterpret_cast<std::uintptr_t>(begin);
		const auto address = reinterpret_cast<std::uintptr_t>(physicalAddress);
		const auto endAddress = beginAddress + static_cast<std::uintptr_t>(amx_->stp);
		if (address < beginAddress || address >= endAddress)
		{
			return AMX_ERR_MEMACCESS;
		}

		*amxAddress = static_cast<cell>(address - beginAddress);
		return AMX_ERR_NONE;
	}

	int Push(cell value) override
	{
		return SampAmx::push(amx_, value);
	}

	// The SDK's CallChecked passes null output addresses, which open.mp accepts.
	// SA-MP's AMX (Pawn 3.2) writes through them unconditionally in amx_Allot,
	// so always hand it real storage.
	int PushArray(cell* amxAddress, cell** physicalAddress, const cell array[], int cells) override
	{
		cell unusedAmxAddress = 0;
		cell* unusedPhysicalAddress = nullptr;
		return SampAmx::pushArray(amx_, amxAddress ? amxAddress : &unusedAmxAddress,
			physicalAddress ? physicalAddress : &unusedPhysicalAddress, array, cells);
	}

	int PushString(cell* amxAddress, cell** physicalAddress, StringView source, bool pack, bool useWchar) override
	{
		const std::string value(source.data(), source.length());
		cell unusedAmxAddress = 0;
		cell* unusedPhysicalAddress = nullptr;
		return SampAmx::pushString(amx_, amxAddress ? amxAddress : &unusedAmxAddress,
			physicalAddress ? physicalAddress : &unusedPhysicalAddress, value.c_str(), pack ? 1 : 0, useWchar ? 1 : 0);
	}

	int Register(const AMX_NATIVE_INFO* natives, int count) override
	{
		return SampAmx::registerNatives(amx_, natives, count);
	}

	int Release(cell address) override
	{
		return SampAmx::release(amx_, address);
	}

	int SetString(cell* destination, StringView source, bool pack, bool useWchar, size_t size) const override
	{
		const std::string value(source.data(), source.length());
		return SampAmx::setString(destination, value.c_str(), pack ? 1 : 0, useWchar ? 1 : 0, size);
	}

	int StrLen(const cell* source, int* length) const override
	{
		return SampAmx::strLen(source, length);
	}

	cell GetCIP() const override { return amx_ ? amx_->cip : 0; }
	cell GetHEA() const override { return amx_ ? amx_->hea : 0; }
	cell GetSTP() const override { return amx_ ? amx_->stp : 0; }
	cell GetSTK() const override { return amx_ ? amx_->stk : 0; }
	cell GetHLW() const override { return amx_ ? amx_->hlw : 0; }
	cell GetFRM() const override { return amx_ ? amx_->frm : 0; }

	void SetCIP(cell value) override { if (amx_) amx_->cip = value; }
	void SetHEA(cell value) override { if (amx_) amx_->hea = value; }
	void SetSTP(cell value) override { if (amx_) amx_->stp = value; }
	void SetSTK(cell value) override { if (amx_) amx_->stk = value; }
	void SetHLW(cell value) override { if (amx_) amx_->hlw = value; }
	void SetFRM(cell value) override { if (amx_) amx_->frm = value; }

	AMX* GetAMX() override { return amx_; }
	void PrintError(int error) override
	{
		// Runtime errors inside AudioCast callbacks would otherwise vanish.
		if (logprintf && error != AMX_ERR_NONE) logprintf("[AudioCast] AMX error %d while running an AudioCast callback", error);
	}

	int GetID() const override { return id_; }
	bool IsLoaded() const override { return loaded_ && amx_ != nullptr; }
};

class SampPawnComponent final : public IPawnComponent
{
private:
	std::vector<std::unique_ptr<SampPawnScript>> scripts_;
	std::vector<IPawnScript*> sideScripts_;
	Impl::DefaultEventDispatcher<PawnEventHandler> eventDispatcher_;
	int nextScriptId_ = 1;

	void rebuildSideScripts() noexcept;
	SampPawnScript* findSampScript(AMX* amx) const noexcept;

public:
	PROVIDE_UID(PawnComponent_UID);

	StringView componentName() const override { return "pawn"; }
	SemanticVersion componentVersion() const override { return SemanticVersion(1, 0, 0); }
	void onLoad(ICore*) override { }
	void onInit(IComponentList*) override { }
	void onReady() override { }
	void onFree(IComponent*) override { }
	void provideConfiguration(ILogger&, IEarlyConfig&, bool) override { }
	void free() override { clear(); }
	void reset() override { clear(); }

	IEventDispatcher<PawnEventHandler>& getEventDispatcher() override { return eventDispatcher_; }
	const StaticArray<void*, NUM_AMX_FUNCS>& getAmxFunctions() const override;
	IPawnScript* getScript(AMX* amx) override;
	const IPawnScript* getScript(AMX* amx) const override;
	IPawnScript* mainScript() override;
	const Span<IPawnScript*> sideScripts() override;

	void load(AMX* amx);
	void unload(AMX* amx);
	void clear();
};
