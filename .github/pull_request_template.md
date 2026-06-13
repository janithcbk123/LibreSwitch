**What this changes**


**Why**


**Checklist**
- [ ] `./test/host/run_all.sh` passes (192/0 baseline)
- [ ] Platform APIs verified against installed LibreTiny source (not from memory)
- [ ] No secrets/certs/credentials committed
- [ ] Hardware-specific config done via build flags where possible
- [ ] If this touches relay control / boot state / factory reset, I've explained
      why the safety invariants still hold

**Tested on hardware?**
- [ ] Yes (model: ____, log attached)
- [ ] No (logic-only change, host tests cover it)
- [ ] N/A (docs)
