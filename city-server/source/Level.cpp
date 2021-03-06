#include "Level.h"
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <fstream>
#include <ctime>
#include <algorithm>

using namespace boost;

const float Level::REAL_HEALHT_FACTOR = 2.0f;
const float Level::REAL_ATTACK_FACTOR = 0.5f;
const float Level::MINIMAL_ATTACK_FACTOR = 0.75f;
const float Level::MAXIMAL_ATTACK_FACTOR = 1.25f;
const regex Level::LEVEL_FILE_LINE_PATTERN(
	"(0|[1-9]\\d*) (tree|mountain|castle) (0|[1-9]\\d*) (0|[1-9]\\d*)"
);

Level::Level(const std::string& filename) :
	last_id(0),
	last_skeleton_adding_timestamp(0)
{
	std::srand(std::time(NULL));

	int minimal_x = 0;
	int minimal_y = 0;
	int maximal_x = 0;
	int maximal_y = 0;

	std::ifstream in(filename.c_str());
	while (in) {
		std::string line;
		std::getline(in, line);
		if (line.empty()) {
			continue;
		}

		smatch matches;
		if (regex_match(line, matches, LEVEL_FILE_LINE_PATTERN)) {
			std::string entity_type = matches[2];
			if (
				entity_type == "tree"
				|| entity_type == "mountain"
				|| entity_type == "castle"
			) {
				last_id = lexical_cast<size_t>(matches[1]);

				Position position(
					lexical_cast<int>(matches[3]),
					lexical_cast<int>(matches[4])
				);

				held_positions.push_back(position);
				if (entity_type == "castle") {
					castles[last_id] = CastleSmartPointer(
						new Castle(INVALID_ID)
					);
					castles[last_id]->position = position;
				}

				if (position.x < minimal_x) {
					minimal_x = position.x;
				} else if (position.x > maximal_x) {
					maximal_x = position.x;
				}
				if (position.y < minimal_y) {
					minimal_y = position.y;
				} else if (position.y > maximal_y) {
					maximal_y = position.y;
				}
			} else {
				std::cerr
					<< (format(
						"Warning! Invalid entity type \"%s\" in level file.\n"
					) % entity_type).str();
				continue;
			}
		} else {
			std::cerr
				<< (format("Warning! Invalid line \"%s\" in level file.\n")
					% line).str();
		}
	}

	for (int x = minimal_x; x <= maximal_x; ++x) {
		for (int y = minimal_y; y <= maximal_y; ++y) {
			Position position(x, y);
			if (!isPositionHeld(position)) {
				not_held_positions.push_back(position);
			}
		}
	}

	last_skeleton_adding_timestamp = std::time(NULL);
}

std::string Level::toString(size_t player_id) {
	lock_guard<boost::mutex> guard(mutex);

	std::string result;
	std::map<size_t, CastleSmartPointer>::const_iterator i = castles.begin();
	for (; i != castles.end(); ++i) {
		size_t castle_state =
			i->second->owner == INVALID_ID
				? 0
				: i->second->owner == player_id
					? 1
					: 2;
		result +=
			(format("c:%u:%u:%u;")
				% i->first
				% i->second->health
				% castle_state).str();
	}
	std::map<size_t, PlayerSmartPointer>::const_iterator j = players.begin();
	for (; j != players.end(); ++j) {
		result +=
			(format("p:%u:%u:%i:%i;")
				% j->first
				% j->second->health
				% j->second->position.x
				% j->second->position.y).str();
	}
	std::map<size_t, SkeletonSmartPointer>::const_iterator k =
		skeletons.begin();
	for (; k != skeletons.end(); ++k) {
		result +=
			(format("s:%u:%u:%i:%i;")
				% k->first
				% k->second->health
				% k->second->position.x
				% k->second->position.y).str();
	}
	result = result.substr(0, result.length() - 1);

	return result;
}

size_t Level::addPlayer(void) {
	lock_guard<boost::mutex> guard(mutex);

	last_id++;

	players[last_id] = PlayerSmartPointer(new Player());
	players[last_id]->position = getRandomUnholdPosition();
	holdPosition(players[last_id]->position);
	players[last_id]->health = getDefaultHealth(last_id);

	return last_id;
}

bool Level::movePlayer(size_t player_id, Direction direction) {
	lock_guard<boost::mutex> guard(mutex);

	if (!players.count(player_id)) {
		throw std::runtime_error("invalid player id");
	}

	/*if (!players[player_id]->timeout()) {
		return false;
	}
	players[player_id]->update();*/

	Position position = players[player_id]->position;
	switch (direction) {
		case DIRECTION_UP:
			position.y--;
			break;
		case DIRECTION_RIGHT:
			position.x++;
			break;
		case DIRECTION_DOWN:
			position.y++;
			break;
		case DIRECTION_LEFT:
			position.x--;
			break;
	}

	bool can_move = !isPositionHeld(position);
	if (can_move) {
		holdPosition(position);
		unholdPosition(players[player_id]->position);

		players[player_id]->position = position;
	} else {
		size_t attack_value = getAttackValue(players[player_id]->health);
		size_t enemy_id = getPlayerByPosition(position);
		if (enemy_id != INVALID_ID) {
			decreasePlayerHealth(enemy_id, attack_value);
		} else {
			size_t enemy_id = getCastleByPosition(position);
			if (enemy_id != INVALID_ID) {
				if (castles[enemy_id]->owner != player_id) {
					decreaseCastleHealth(enemy_id, attack_value, player_id);
				}
			} else {
				size_t skeleton_id = getSkeletonByPosition(position);
				if (skeleton_id != INVALID_ID) {
					decreaseSkeletonHealth(skeleton_id, attack_value);
				}
			}
		}
	}

	return can_move;
}

void Level::updatePlayerTimestamp(size_t player_id) {
	lock_guard<boost::mutex> guard(mutex);

	if (!players.count(player_id)) {
		throw std::runtime_error("invalid player id");
	}

	players.at(player_id)->timestamp = std::time(NULL);
}

void Level::removeLostPlayers(void) {
	lock_guard<boost::mutex> guard(mutex);

	std::map<size_t, PlayerSmartPointer>::iterator i = players.begin();
	time_t current_timestamp = std::time(NULL);
	while (i != players.end()) {
		if (
			current_timestamp - i->second->timestamp >= MAXIMAL_PLAYER_TIMEOUT
		) {
			unholdPosition(i->second->position);
			unholdCastles(i->first);
			players.erase(i++);
		} else {
			++i;
		}
	}
}

void Level::updateCastles(void) {
	lock_guard<boost::mutex> guard(mutex);

	time_t current_timestamp = std::time(NULL);
	std::map<size_t, CastleSmartPointer>::const_iterator i = castles.begin();
	for (; i != castles.end(); ++i) {
		// пополняем отряды владельцев замков
		if (players.count(i->second->owner)) {
			time_t elapsed_time = current_timestamp - i->second->timestamp;
			if (elapsed_time >= MAXIMAL_CASTLE_TIMEOUT) {
				players[i->second->owner]->health +=
					elapsed_time / MAXIMAL_CASTLE_TIMEOUT;
				i->second->timestamp = current_timestamp;
			}
		}

		// отвечаем на атаки противников
		if (!i->second->timeout()) {
			continue;
		}
		i->second->update();

		std::set<size_t>::const_iterator j = i->second->enemies.begin();
		while (j != i->second->enemies.end()) {
			size_t player_id = *j++;
			if (players.count(player_id)) {
				int delta_x = std::abs(
					players[player_id]->position.x - i->second->position.x
				);
				int delta_y =std::abs(
					players[player_id]->position.y - i->second->position.y
				);
				if (
					(delta_x == 1 && delta_y == 0)
					|| (delta_x == 0 && delta_y == 1)
				) {
					size_t attack_value = getAttackValue(i->second->health);
					decreasePlayerHealth(player_id, attack_value);
				} else {
					i->second->enemies.erase(player_id);
				}
			}
		}
	}
}

void Level::addSkeleton(void) {
	lock_guard<boost::mutex> guard(mutex);

	time_t current_timestamp = std::time(NULL);
	if (
		current_timestamp - last_skeleton_adding_timestamp
			> SKELETON_ADDING_TIMEOUT
		&& skeletons.size() < MAXIMAL_SKELETONS_NUMBER
	) {
		last_id++;

		size_t health = getDefaultHealth(INVALID_ID);
		skeletons[last_id] = SkeletonSmartPointer(new Skeleton(health));
		skeletons[last_id]->position = getRandomUnholdPosition();
		holdPosition(skeletons[last_id]->position);

		last_skeleton_adding_timestamp = current_timestamp;
	}
}

void Level::updateSkeletons(void) {
	std::map<size_t, SkeletonSmartPointer>::const_iterator i =
		skeletons.begin();
	for (; i != skeletons.end(); ++i) {
		if (!i->second->timeout()) {
			continue;
		}
		i->second->update();

		skeletonRandomMove(i->first);

		for (
			std::map<size_t, PlayerSmartPointer>::const_iterator j =
				players.begin();
			j != players.end();
			++j
		) {
			size_t player_id = j->first;
			int delta_x = std::abs(
				players[player_id]->position.x - i->second->position.x
			);
			int delta_y =std::abs(
				players[player_id]->position.y - i->second->position.y
			);
			if (
				(delta_x == 1 && delta_y == 0)
				|| (delta_x == 0 && delta_y == 1)
			) {
				size_t attack_value = getAttackValue(i->second->health);
				decreasePlayerHealth(player_id, attack_value);
			}
		}
	}
}

bool Level::isPositionHeld(const Position& position) const {
	return
		std::find(held_positions.begin(), held_positions.end(), position)
		!= held_positions.end();
}

void Level::holdPosition(const Position& position) {
	if (!isPositionHeld(position)) {
		held_positions.push_back(position);

		not_held_positions.erase(
			std::remove(
				not_held_positions.begin(),
				not_held_positions.end(),
				position
			),
			not_held_positions.end()
		);
	}
}

void Level::unholdPosition(const Position& position) {
	if (isPositionHeld(position)) {
		held_positions.erase(
			std::remove(
				held_positions.begin(),
				held_positions.end(),
				position
			),
			held_positions.end()
		);

		not_held_positions.push_back(position);
	}
}

size_t Level::getAttackValue(size_t base_value) const {
	float attack_factor =
		static_cast<float>(std::rand())
		/ RAND_MAX
		* (MAXIMAL_ATTACK_FACTOR - MINIMAL_ATTACK_FACTOR)
		+ MINIMAL_ATTACK_FACTOR;
	float advance_value = REAL_ATTACK_FACTOR * base_value / REAL_HEALHT_FACTOR;
	size_t value = static_cast<size_t>(
		std::floor(attack_factor * advance_value + 0.5f)
	);
	if (value == 0) {
		value = 1;
	}

	return value;
}

size_t Level::getCastleByPosition(const Position& position) const {
	std::map<size_t, CastleSmartPointer>::const_iterator i = castles.begin();
	for (; i != castles.end(); ++i) {
		if (i->second->position == position) {
			return i->first;
		}
	}

	return INVALID_ID;
}

void Level::decreaseCastleHealth(
	size_t castle_id,
	size_t value,
	size_t player_id
) {
	if (castles[castle_id]->health > value) {
		castles[castle_id]->health -= value;
		castles[castle_id]->enemies.insert(player_id);
	} else {
		resetCastle(castle_id, player_id);
	}
}

void Level::resetCastle(size_t castle_id, size_t player_id) {
	castles[castle_id]->timestamp = std::time(NULL);
	castles[castle_id]->health = Castle::DEFAULT_HEALTH;
	castles[castle_id]->owner = player_id;
	castles[castle_id]->enemies.erase(player_id);
}

void Level::unholdCastles(size_t player_id) {
	std::map<size_t, CastleSmartPointer>::const_iterator i = castles.begin();
	for (; i != castles.end(); ++i) {
		if (i->second->owner == player_id) {
			i->second->health = 0;
			i->second->owner = INVALID_ID;
		} else {
			i->second->enemies.erase(player_id);
		}
	}
}

Position Level::getRandomUnholdPosition(void) const {
	if (not_held_positions.empty()) {
		throw std::runtime_error("all positions're held");
	}

	return not_held_positions[std::rand() % not_held_positions.size()];
}

size_t Level::getDefaultHealth(size_t exception_id) const {
	if (players.size() == 1) {
		return players.begin()->second->health;
	} else if (players.size() > 1) {
		size_t health_sum = 0;
		std::map<size_t, PlayerSmartPointer>::const_iterator i =
			players.begin();
		for (; i != players.end(); ++i) {
			if (i->first != exception_id) {
				health_sum += i->second->health;
			}
		}

		return health_sum / (players.size() - 1);
	}

	return Player::DEFAULT_HEALTH;
}

size_t Level::getPlayerByPosition(const Position& position) const {
	std::map<size_t, PlayerSmartPointer>::const_iterator i = players.begin();
	for (; i != players.end(); ++i) {
		if (i->second->position == position) {
			return i->first;
		}
	}

	return INVALID_ID;
}

void Level::decreasePlayerHealth(size_t player_id, size_t value) {
	if (players[player_id]->health > value) {
		players[player_id]->health -= value;
	} else {
		resetPlayer(player_id);
	}
}

void Level::resetPlayer(size_t player_id) {
	unholdPosition(players[player_id]->position);
	players[player_id]->position = getRandomUnholdPosition();
	holdPosition(players[player_id]->position);

	players[player_id]->health = getDefaultHealth(player_id);

	unholdCastles(player_id);
}

size_t Level::getSkeletonByPosition(const Position& position) const {
	std::map<size_t, SkeletonSmartPointer>::const_iterator i =
		skeletons.begin();
	for (; i != skeletons.end(); ++i) {
		if (i->second->position == position) {
			return i->first;
		}
	}

	return INVALID_ID;
}

void Level::decreaseSkeletonHealth(size_t skeleton_id, size_t value) {
	if (skeletons[skeleton_id]->health > value) {
		skeletons[skeleton_id]->health -= value;
	} else {
		killSkeleton(skeleton_id);
	}
}

void Level::killSkeleton(size_t skeleton_id) {
	unholdPosition(skeletons[skeleton_id]->position);
	skeletons.erase(skeleton_id);

	last_skeleton_adding_timestamp = std::time(NULL);
}

std::vector<Position> Level::getNeighborhood(const Position& position) const {
	std::vector<Position> shifts;
	shifts.push_back(Position(-1, 0));
	shifts.push_back(Position(1, 0));
	shifts.push_back(Position(0, -1));
	shifts.push_back(Position(0, 1));

	std::vector<Position> positions;
	for (
		std::vector<Position>::const_iterator i = shifts.begin();
		i != shifts.end();
		++i
	) {
		Position new_position(position.x + i->x, position.y + i->y);
		if (!isPositionHeld(new_position)) {
			positions.push_back(new_position);
		}
	}

	return positions;
}

void Level::skeletonRandomMove(size_t skeleton_id) {
	std::vector<Position> positions = getNeighborhood(
		skeletons[skeleton_id]->position
	);
	if (!positions.empty()) {
		std::random_shuffle(positions.begin(), positions.end());

		unholdPosition(skeletons[skeleton_id]->position);
		skeletons[skeleton_id]->position = positions.front();
		holdPosition(skeletons[skeleton_id]->position);
	}
}
