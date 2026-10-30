Super Fighters configuration: perks, skills, tokens and daily events.

Files here are read when the game starts. To change them without rebuilding the VPK, copy a file to
  ux0:data/superfighters/config/   (Vita)
  ~/.superfighters/config/         (PC)
A file there replaces the one here. If a file has a JSON error the game writes it to log.txt and uses this one.

perks.json
  weaponGroups: which weapons a perk of that group applies to.
  branches:     the three gym stations.
  perks:        id (unique), name, desc, cost, "group" (weapon perk) or "branch" (fighter perk), effects.

  Weapon effects (a number each; Mul multiplies, Add adds):
    ammoMul damageMul knockbackMul recoilMul spreadMul timingMul countAdd countSet drawFrames
    pierce rideMul directExplode blastRadiusMul blastDamageMul
    thrownMul meleeDamageMul durabilityMul throwSpeedMul fuseMul
  Fighter effects:
    throwMul comboHits finisherMul grabMul diveMul climbMul shardImmune stunMul fallDamageMul breakFreeMul

  A new perk made of these effects needs no code. A new kind of effect does.

skills.json
  type: dash (amount = speed), counter (amount = damage returned), heal (amount), shout (radius, duration),
        smoke (duration), rage (amount = knockback multiplier, duration).
  cooldown and duration are in frames, 60 = one second.

progress.json
  tokens: round, kill, win, wave = rewards. difficulty: multipliers for easy, normal, hard. ranks: earned tokens.

daily.json
  events: multiplier, optional "map" and "mode" (0 free for all, 1 teams, 2 waves). Without them it applies everywhere.
  contracts: stat, target, reward.
  The day's picks come from the date, so everyone gets the same ones.
