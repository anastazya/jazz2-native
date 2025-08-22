﻿#pragma once

#include "../ActorBase.h"
#include "../../Direction.h"

namespace Jazz2::Actors::Enemies
{
	/** @brief Base class of an enemy */
	class EnemyBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		EnemyBase();

		/** @brief Whether the enemy should collide with player shots */
		bool CanCollideWithShots;

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		/** @brief Whether the enemy can hurt player */
		bool CanHurtPlayer() const {
			return (_canHurtPlayer && _frozenTimeLeft <= 0.0f);
		}

		/** @brief Whether the enemy is currently frozen */
		bool IsFrozen() const {
			return (_frozenTimeLeft > 0.0f);
		}

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		bool _canHurtPlayer;
		std::uint32_t _scoreValue;
#endif

		void OnUpdate(float timeMult) override;
		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;

		void AddScoreToCollider(ActorBase* collider);
		virtual void SetHealthByDifficulty(std::int32_t health);
		void PlaceOnGround();
		bool CanMoveToPosition(float x, float y);
		void TryGenerateRandomDrop();
		void StartBlinking();
		void HandleBlinking(float timeMult);

	private:
		float _blinkingTimeout;
	};
}