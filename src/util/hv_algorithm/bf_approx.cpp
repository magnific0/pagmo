/*****************************************************************************
 *   Copyright (C) 2004-2013 The PaGMO development team,                     *
 *   Advanced Concepts Team (ACT), European Space Agency (ESA)               *
 *   http://apps.sourceforge.net/mediawiki/pagmo                             *
 *   http://apps.sourceforge.net/mediawiki/pagmo/index.php?title=Developers  *
 *   http://apps.sourceforge.net/mediawiki/pagmo/index.php?title=Credits     *
 *   act@esa.int                                                             *
 *                                                                           *
 *   This program is free software; you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation; either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program; if not, write to the                           *
 *   Free Software Foundation, Inc.,                                         *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.               *
 *****************************************************************************/


#include "bf_approx.h"

namespace pagmo { namespace util { namespace hv_algorithm {

bf_approx::bf_approx(const double eps, const double delta, const double gamma, const double delta_multiplier, const double start_delta)
	: m_eps(eps), m_delta(delta), m_gamma(gamma), m_delta_multiplier(delta_multiplier), m_start_delta(start_delta) { }

bf_approx::bf_approx(const bf_approx &orig)
	: m_eps(orig.m_eps), m_delta(orig.m_delta), m_gamma(orig.m_gamma), m_delta_multiplier(orig.m_delta_multiplier), m_start_delta(orig.m_start_delta) { }

/// Least contributor method
/**
 * This method establishes the individual that contributes the least to the hypervolume.
 *
 * @param[in] points vector of fitness_vectors for which the hypervolume is computed
 * @param[in] r_point distinguished "reference point".
 *
 * @return index of the least contributing point
 */
unsigned int bf_approx::least_contributor(const std::vector<fitness_vector> &points, const fitness_vector &r_point) {

	m_no_samples = std::vector<unsigned long long>(points.size(), 0);
	m_no_succ_samples = std::vector<unsigned long long>(points.size(), 0);
	m_no_ops = std::vector<unsigned long long>(points.size(), 0);
	m_point_set = std::vector<unsigned int>(points.size(), 0);
	m_box_volume = std::vector<double>(points.size(), 0.0);
	m_approx_volume = std::vector<double>(points.size(), 0.0);
	m_point_delta = std::vector<double>(points.size(), 0.0);
	m_boxes = std::vector<fitness_vector>(points.size());
	m_box_points = std::vector<std::vector<unsigned int> >(points.size());
	m_log_factor = log (2. * points.size() * (1. + m_gamma) / (m_delta * m_gamma) );

	// round counter
	unsigned int round_no = 0;
	// round delta
	double r_delta = 0.0;

	bool stop_condition = false;
	unsigned int LC = 0;

	// put every point into the set
	for(unsigned int i = 0 ; i < m_point_set.size() ; ++i) {
		m_point_set[i] = i;
	}

	// Initial computation
	// - compute bounding boxes and their hypervolume
	// - set round Delta as max of hypervolumes
	// - determine points overlapping with each bounding box
	for(std::vector<fitness_vector>::size_type idx = 0 ; idx < points.size() ; ++idx) {
		m_boxes[idx] = compute_bounding_box(points, r_point, idx);
		m_box_volume[idx] = base::volume_between(points[idx], m_boxes[idx]);
		r_delta = fmax(r_delta, m_box_volume[idx]);

		for(std::vector<fitness_vector>::size_type idx2 = 0 ; idx2 < points.size() ; ++idx2) {
			if (idx == idx2) {
				continue;
			}
			int op = point_in_box(points[idx2], points[idx], m_boxes[idx]);
			switch(op) {
				case 1:
					m_box_points[idx].push_back(idx2);
					break;
				case 2:
					// since contribution by idx is guaranteed to be 0.0 (as the point is dominated) we might as well return it as the least contributor right away
					return idx;
				case 3:
					// points at idx and idx2 are equal, each of them will contribute 0.0 hypervolume, return idx as the least contributor
					return idx;
				default:
					break;
			}
		}
	}

	// decrease the initial maximum volume by a constant factor
	r_delta *= m_start_delta;

	// Main loop
	do {
		r_delta *= m_delta_multiplier;
		++round_no;

		for(unsigned int _i = 0 ; _i < m_point_set.size() ; ++_i) {
			unsigned int idx = m_point_set[_i];
			do {
				++m_no_samples[idx];
				if (sample_successful(points, idx)) {
					++m_no_succ_samples[idx];
				}
				m_approx_volume[idx] = static_cast<double>(m_no_succ_samples[idx]) / static_cast<double>(m_no_samples[idx]) * m_box_volume[idx];
				m_point_delta[idx] = chernoff(round_no, idx) * m_box_volume[idx];
			} while (m_point_delta[idx] > r_delta);

			// update the least contributor
			if(m_approx_volume[LC] > m_approx_volume[idx]) {
				LC = idx;
			}
		}

		std::vector<unsigned int>::iterator it = m_point_set.begin();
		while(it != m_point_set.end()) {
			unsigned int idx = *it;
			if ( (idx != LC) && ( (m_approx_volume[idx] - m_point_delta[idx]) > (m_approx_volume[LC] + m_point_delta[LC]) ) ) {
				it = m_point_set.erase(it);
			}
			else {
				++it;
			}
		}


		stop_condition = false;
		if (m_point_set.size() <= 1) {
			stop_condition = true;
		} else {
			stop_condition = true;
			for(unsigned int _i = 0 ; _i < m_point_set.size() ; ++_i) {
				unsigned int idx = m_point_set[_i];
				if (idx == LC) {
					continue;
				}
				double d = ( m_approx_volume[LC] + m_point_delta[LC] ) / ( m_approx_volume[idx] - m_point_delta[idx] );
				if( d <= 0 || d > 1 + m_eps ) {
					stop_condition = false;
					break;
				}
			}
		}

	} while (!stop_condition);

	return LC;
}

/// samples the bounding box and returns true if it fell into the exclusive hypervolume
bool bf_approx::sample_successful(const std::vector<fitness_vector> &points, const unsigned int idx) {
	const fitness_vector &lb = points[idx];
	const fitness_vector &ub = m_boxes[idx];
	fitness_vector rnd_p(lb.size(), 0.0);
	for(unsigned int i = 0 ; i < lb.size() ; ++ i) {
		rnd_p[i] = lb[i] + m_drng()*(ub[i]-lb[i]);
	}

	for(unsigned int i = 0 ; i < m_box_points[idx].size() ; ++i) {
		const fitness_vector &box_p = points[m_box_points[idx][i]];
		bool dominates = true;
		m_no_ops[idx] += box_p.size() + 1;
		for(unsigned int d_idx = 0 ; d_idx < box_p.size() ; ++d_idx) {
			if(rnd_p[d_idx] < box_p[d_idx]) {
				dominates=false;
				break;
			}
		}
		if (dominates) {
			return false;
		}
	}
	return true;
}


double bf_approx::chernoff(const unsigned int round_no, const unsigned int idx) const {
	return sqrt(
			0.5 * ((1. + m_gamma) * log(static_cast<double>(round_no)) + m_log_factor)
			/ (static_cast<double>(m_no_samples[idx]))
		);
}

/// dominance comparison
/**
 * Establishes the domination relationship between two points.
 *
 * return 1 if 'a' IS DOMINATED BY 'b'
 * return 2 if 'a' DOMINATES 'b'
 * return 3 if 'a' IS EQUAL TO 'b'
 * return 4 otherwise
 */
int bf_approx::dom_cmp(const fitness_vector &a, const fitness_vector &b) const {
	for(fitness_vector::size_type i = 0; i < a.size() ; ++i) {
		if (a[i] > b[i]) {
			for(fitness_vector::size_type j = i + 1; j < a.size() ; ++j) {
				if (a[j] < b[j]) {
					return 4;
				}
			}
			return 1;
		}
		else if (a[i] < b[i]) {
			for(fitness_vector::size_type j = i + 1 ; j < a.size() ; ++j) {
				if (a[j] > b[j]) {
					return 4;
				}
			}
			return 2;
		}
	}
	return 3;
}

// return 0 - box (p, R) has no overlapping volume with box (a, b)
// return 1 - box (p, R) overlaps some volume with the box (a,b)
// return 2 - point p dominates the point a (in which case, contribution by box (a,b) is guaranteed to be 0)
// return 3 - point p is equal to point a (in which case we remove one of them)
// R is reference point (implicit)
int bf_approx::point_in_box(const fitness_vector &p, const fitness_vector &a, const fitness_vector &b) const { 
	int cmp_a_p = dom_cmp(a, p);

	// point a is equal to point p (duplicate)
	if (cmp_a_p == 3) {
		return 3;
	// point a is dominated by p (a is the least contributor)
	} else if (cmp_a_p == 1) {
		return 2;
	} else if(dom_cmp(b, p) == 1) {
		return 1;
	} else {
		return 0;
	}
}

/// Compute bounding box method
/* Find the MINIMAL (in terms of volume) bounding box that contains all the exclusive hypervolume contributed by point at index 'p_idx'
 * Thus obtained point 'z' and the original point 'points[p_idx]' form a box in which we perform the monte carlo sampling
 *
 * @param[in] points pareto front points
 * @param[in] r_point reference point
 * @param[in] p_idx index of point for which we compute the bounding box
 *
 * @return fitness_vector describing the opposite corner of the bounding box
 */
fitness_vector bf_approx::compute_bounding_box(const std::vector<fitness_vector> &points, const fitness_vector &r_point, const unsigned int p_idx) const {
	// z is the opposite corner of the bounding box (reference point as a 'safe' first candidate - this is the MAXIMAL bounding box as of yet)
	fitness_vector z(r_point);

	// Below we perform a reduction to the minimal bounding box.
	// We check whether given point at 'idx' is DOMINATED (strong domination) by 'p_idx' in exactly one objective, and DOMINATING (weak domination) in the remaining ones.
	const fitness_vector& p = points[p_idx];
	int worse_dim_idx;
	unsigned int f_dim = r_point.size();
	for(std::vector<fitness_vector>::size_type idx = 0 ; idx < points.size(); ++idx) {

		worse_dim_idx = -1;  // initiate the possible opposite point dimension by -1 (no candidate)

		for(fitness_vector::size_type f_idx = 0; f_idx < f_dim; ++f_idx) {
			if (points[idx][f_idx] >= p[f_idx]) {  // if any point is worse by given dimension, it's the potential dimension in which we reduce the box
				if (worse_dim_idx != -1) {  // if given point is already worse in any previous dimension, skip to next point as it's a bad candidate
					worse_dim_idx = -1;  // set the result to "no candidate" and break
					break;
				}
				worse_dim_idx = f_idx;
			}
		}
		if (worse_dim_idx != -1){  // if given point was worse only in one dimension it's the potential candidate for the bouding box reductor
			z[worse_dim_idx] = fmin(z[worse_dim_idx], points[idx][worse_dim_idx]);  // reduce the bounding box
		}
	}
	return z;
}

// verify_before_compute
/**
 * Verifies whether given algorithm suits the requested data.
 *
 * @param[in] points vector of points containing the d dimensional points for which we compute the hypervolume
 * @param[in] r_point reference point for the vector of points
 *
 * @throws value_error when trying to compute the hypervolume for the non-maximal reference point
 */
void bf_approx::verify_before_compute(const std::vector<fitness_vector> &points, const fitness_vector &r_point) {
	base::assert_maximal_reference_point(points, r_point);
}

double bf_approx::compute(const std::vector<fitness_vector> &points, const fitness_vector &r_point) {
	(void)points;
	(void)r_point;
	pagmo_throw(value_error, "Compute hypervolume method is not yet implemented.");
	return 0.0;
}

/// Clone method.
base_ptr bf_approx::clone() const
{
	return base_ptr(new bf_approx(*this));
}

/// Algorithm name
std::string bf_approx::get_name() const {
	return "Bringmann-Friedrich approximation method";
}

} } }

BOOST_CLASS_EXPORT_IMPLEMENT(pagmo::util::hv_algorithm::bf_approx);
