#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

#include "cgal_typedef.h"
#include "map_state.h"
#include "geo_div.h"
#include "pll.h"
#include "densify.h"

// Inserts a polyline into the graph
void insert(const std::vector<Point>& poly, Graph& graph, Point_vertex_map& pvmap) {
  vertex_descriptor u, v;
  for (int i = 0; i < (int) poly.size(); i++) {
    // check if the point is not yet in the graph
    if (pvmap.find(poly[i]) == pvmap.end()) {
      v = add_vertex(graph);
      pvmap[poly[i]] = v;
    } else {
      v = pvmap[poly[i]];
    }
    graph[v] = poly[i];  // associate the point to the vertex
    if (i != 0) {
      add_edge(u, v, graph);
    }
    u = v;
  }
}

template <typename Graph>
struct Polyline_visitor {
  std::list<Polyline>& polylines;
  const Graph& points_pmap;

  Polyline_visitor(std::list<Polyline>& lines, const Graph& points_property_map)
    : polylines(lines), points_pmap(points_property_map)
  {}

  void start_new_polyline() {
    Polyline V;
    polylines.push_back(V);
  }

  void add_node(typename boost::graph_traits<Graph>::vertex_descriptor vd) {
    Polyline& polyline = polylines.back();
    polyline.push_back(points_pmap[vd]);
  }

  void end_polyline() {}
};

Graph create_pll_graph(std::vector<GeoDiv> container) {
  Graph graph;
  Point_vertex_map pvmap;
  for (GeoDiv gd : container) {
    for (Polygon_with_holes pgnwh : gd.polygons_with_holes()) {
      Polyline pll_outer; // polyline for outer boundary
      Polygon outer = pgnwh.outer_boundary();
      for (Point pt_outer : outer)
        pll_outer.push_back(pt_outer);
      insert(pll_outer, graph, pvmap); // insert pll_outer into graph

      std::vector<Polygon> holes_v(pgnwh.holes_begin(), pgnwh.holes_end());
      for (Polygon hole : holes_v) {
        Polyline pll_hole; // polyline for hole
        for (Point pt_hole : hole)
          pll_hole.push_back(pt_hole);
        insert(pll_hole, graph, pvmap); // insert pll_hole into graph
      }
    }
  }
  return graph;
}

void print_pll(PLL pll) {
  std::cout << pll.get_gd_v()[0];
  std::cout << " | " << pll.get_pgnwh_v()[0];
  std::cout << " | " << pll.get_pos();
  std::cout << " | " << pll.get_bool_hole();
  std::cout << " | " << pll.get_v1();
  std::cout << " | " << pll.get_vl();
  std::cout << " | " << pll.get_v2() << std::endl; 
}

std::map<int, Polyline> store_polyline_dens_to_org(CT ct, std::list<Polyline> polyline_list) {
  std::map<int, Polyline> pll_dens_to_org; 
  int pos = 0;
  for (auto cit = ct.constraints_begin(); cit != ct.constraints_end(); cit++) {
    bool polyline_match = false;
    for (Polyline polyl : polyline_list) {
      int same_v = 0;
      for (Point pt : polyl) {
        for (auto vit = ct.points_in_constraint_begin(*cit); vit != ct.points_in_constraint_end(*cit); vit++) {
          if (pt == *vit) {
            same_v++;
            if (same_v >= 3 || (polyl.size() == 2 && same_v == 2)) { // or only 2 vertices in polyline
              polyline_match = true;
              pll_dens_to_org[pos] = polyl;
            }
            break;
          }
        }
        if (polyline_match) break;
      }
      if (polyline_match) break;
    }
    pos++;
  }
  return pll_dens_to_org;
}

std::map<int, std::vector<PLL>> store_by_pos(CT &ct, 
                                             std::vector<GeoDiv> container,
                                             std::map<int, Polyline> pll_dens_to_org) {
  std::map<int, std::vector<PLL>> pll_cntr_by_pos; 
  int pos = 0;
  for (auto cit = ct.constraints_begin(); cit != ct.constraints_end(); cit++) {
    Polyline polyl;
    for (auto vit = ct.points_in_constraint_begin(*cit); vit != ct.points_in_constraint_end(*cit); vit++)
      polyl.push_back(*vit);

    int gd_num = 0;
    for (GeoDiv gd : container) {
      int pgnwh_num = 0;
      for (Polygon_with_holes pgnwh : gd.polygons_with_holes()) {
        PLL pll_outer(pos, polyl, {gd_num}, {pgnwh_num}, false);
        Polygon outer = pgnwh.outer_boundary();

        // need at least 3 vertices of the pll to be on outer boundary to count as part of polygon
        int num_v_on_outer = 0;
        for (Point pt : pll_outer.get_pll()) {
          bool v_on_outer = CGAL::bounded_side_2(outer.begin(), outer.end(), pt) == CGAL::ON_BOUNDARY;
          if (v_on_outer) num_v_on_outer++; 
          if (num_v_on_outer >= 3) break;
        }

        if (num_v_on_outer >= 3) {
          print_pll(pll_outer);
          pll_cntr_by_pos[pos].push_back(pll_outer);
        } else {
          // If the polyline originally only had 2 vertices, check if the pll is really on the outer
          // Edge case is the border of Missouri, Kentucky, Tennessee
          if (pll_dens_to_org[pos].size() == 2) {
            bool v1_vl_pll_on_outer = false;
            for (int i = 0; i < (int) outer.size() - 1; i++) {
              // accounts for clockwise and counter-clockwise orientation of outer
              bool direction_1 = outer[i] == pll_outer.get_v1() && outer[i + 1] == pll_outer.get_vl();
              bool direction_2 = outer[i + 1] == pll_outer.get_v1() && outer[i] == pll_outer.get_vl();
              if (direction_1 || direction_2) {
                v1_vl_pll_on_outer = true;
                break;
              }
            }
            if (v1_vl_pll_on_outer) {
              //std::cout << pll_dens_to_org[pos].size() << " " << pll_outer.get_pll().size() << std::endl;
              print_pll(pll_outer);
              pll_cntr_by_pos[pos].push_back(pll_outer);
            }
          }
        }

        std::vector<Polygon> holes_v(pgnwh.holes_begin(), pgnwh.holes_end());
        for (Polygon hole : holes_v) {
          PLL pll_hole(pos, polyl, {gd_num}, {pgnwh_num}, true);

          int num_v_on_outer_h = 0;
          for (Point pt_h : pll_outer.get_pll()) {
            bool v_on_outer_h = CGAL::bounded_side_2(hole.begin(), hole.end(), pt_h) == CGAL::ON_BOUNDARY;
            if (v_on_outer_h) num_v_on_outer_h++; 
            if (num_v_on_outer_h >= 2) break;
          }

          if (num_v_on_outer_h >= 2) {
            print_pll(pll_hole);
            pll_cntr_by_pos[pos].push_back(pll_hole);
          }
        }
        pgnwh_num++;
      }
      gd_num++;
    }
    pos++;
  }
  std::cout << std::endl;
  return pll_cntr_by_pos;
}

std::map<int, std::map<int, std::vector<PLL>>> store_by_gd_pgnwh(std::vector<GeoDiv> container, 
    CT &ct, std::map<int, 
    std::vector<PLL>> &pll_cntr_by_pos) {
  std::map<int, std::map<int, std::vector<PLL>>> pll_cntr_by_gd_pgnwh;
  int gd_num = 0;
  for (GeoDiv gd : container) {
    int pgnwh_num = 0;
    for (Polygon_with_holes pgnwh : gd.polygons_with_holes()) {
      int cit_num = 0;
      for (auto cit = ct.constraints_begin(); cit != ct.constraints_end(); cit++) {
        for (PLL pll : pll_cntr_by_pos[cit_num]) {
          if (pll.get_gd_v()[0] == gd_num && pll.get_pgnwh_v()[0] == pgnwh_num) {
            Polyline pll_ct;
            for (auto vit = ct.points_in_constraint_begin(*cit); vit != ct.points_in_constraint_end(*cit); vit++)
              pll_ct.push_back(*vit);
            PLL pll_new(pll.get_pos(), pll_ct, pll.get_gd_v(), pll.get_pgnwh_v(), pll.get_bool_hole());
            pll_cntr_by_gd_pgnwh[gd_num][pgnwh_num].push_back(pll_new);
            break;
          }
        }
        cit_num++;
      }
      pgnwh_num++;
    }
    gd_num++;
  }
  return pll_cntr_by_gd_pgnwh;
}

void label_holes_correctly(std::vector<GeoDiv> container,
    std::map<int, std::map<int, std::vector<PLL>>> &pll_cntr_by_gd_pgnwh) {
  for (auto [gd_num, m] : pll_cntr_by_gd_pgnwh) {
    for (auto [pgnwh_num, v] : m) {
      for (PLL pll : v) {
        int gd_num2 = 0;
        for (GeoDiv gd3 : container) {
          int pgnwh_num2 = 0;
          for (Polygon_with_holes pgnwh3 : gd3.polygons_with_holes()) {
            std::vector<Polygon> holes_v(pgnwh3.holes_begin(), pgnwh3.holes_end());
            if (gd_num2 == gd_num && pgnwh_num2 == pgnwh_num && holes_v.empty()) {
              for (int i = 0; i < (int) pll_cntr_by_gd_pgnwh[gd_num][pgnwh_num].size(); i++)
                pll.set_bool_hole(false);
            } else {
              break;
            }
            pgnwh_num2++;
          }
          gd_num2++;
        }
      }
    }
  }
}

void set_visited_vals(std::unordered_map<int, std::unordered_map<int, std::unordered_map<int, bool>>> &visited,
    std::map<int, std::map<int, std::vector<PLL>>> &pll_cntr_by_gd_pgnwh) {
  for (auto [gd_num, m] : pll_cntr_by_gd_pgnwh) {
    for (auto [pgnwh_num, pll_v] : m) {
      int i = 0;
      for (PLL pll : pll_v) {
        visited[gd_num][pgnwh_num][pll.get_pos()] = false; // Set all visited to false
        i++;
      }
      // Sort each vector<PLL> so that all holes are in front
      auto cmp = [](PLL pll1, PLL pll2) {
        if (pll1.get_bool_hole()) // if pll1 is hole, move to front
          return 1;
        else
          return 0;
      };
      std::sort(pll_cntr_by_gd_pgnwh[gd_num][pgnwh_num].begin(), pll_cntr_by_gd_pgnwh[gd_num][pgnwh_num].end(), cmp);
    }
  }

  // Print out new sequence
  for (auto [gd_num, m] : pll_cntr_by_gd_pgnwh)
    for (auto [pgnwh_num, pll_v] : m)
      for (PLL pll : pll_v)
        print_pll(pll);
  std::cout << std::endl;
}

void assemble_pll_to_pgn(std::map<int, std::map<int, std::vector<PLL>>> &pll_cntr_by_gd_pgnwh, 
    std::unordered_map<int, std::unordered_map<int, std::unordered_map<int, bool>>> &visited, 
    std::vector<GeoDiv> &container_final) {
  for (auto [gd_num, m] : pll_cntr_by_gd_pgnwh) {
    GeoDiv gd_final("gd_final");
    std::vector<Polygon> holes_v;
    for (auto [pgnwh_num, pll_v] : m) {

      for (PLL pll : pll_v) {
        Polygon outer; // This will only be for islands/holes anyway
        if (visited[gd_num][pgnwh_num][pll.get_pos()]) continue;

        std::cout << pll.get_gd_v()[0] << " " << pll.get_pgnwh_v()[0] << " " << pll.get_pos() << " " << pll.get_bool_hole() << std::endl;

        // if it is a single polyline (e.g. island)
        if (pll.get_v1() == pll.get_vl() && !visited[gd_num][pgnwh_num][pll.get_pos()]) {

          visited[gd_num][pgnwh_num][pll.get_pos()] = true;

          for (Point pt : pll.get_pll())
            outer.push_back(pt);

          if (!pll.get_bool_hole()) {
            Polygon_with_holes pgnwh(outer);
            gd_final.push_back(pgnwh);
          } else {
            holes_v.push_back(outer);
          }
        } else {
          std::deque<PLL> deq;
          deq.push_back(pll);
          visited[gd_num][pgnwh_num][pll.get_pos()] = true;

          // Connect together all polylines belonging to a Polygon_with_holes
          while (1) {
            bool found = false;
            for (PLL pll2 : pll_cntr_by_gd_pgnwh[pll.get_gd_v()[0]][pll.get_pgnwh_v()[0]]) {
              if (visited[gd_num][pgnwh_num][pll2.get_pos()] == true) continue;

              if (deq.front().get_v1()[0] == pll2.get_vl()[0] &&
                  deq.front().get_v1()[1] == pll2.get_vl()[1]) {
                deq.push_front(pll2);
                visited[gd_num][pgnwh_num][pll2.get_pos()] = true;
                found = true;
              } else if (deq.back().get_vl()[0] == pll2.get_v1()[0] &&
                  deq.back().get_vl()[1] == pll2.get_v1()[1]) {
                deq.push_back(pll2);
                visited[gd_num][pgnwh_num][pll2.get_pos()] = true;
                found = true;
              } else if (deq.front().get_v1()[0] == pll2.get_v1()[0] &&
                  deq.front().get_v1()[1] == pll2.get_v1()[1]) {
                Polyline polyl_new = pll2.get_pll();
                std::reverse(polyl_new.begin(), polyl_new.end());
                pll2.set_pll(polyl_new, pll2.get_pll_org());
                deq.push_front(pll2);
                visited[gd_num][pgnwh_num][pll2.get_pos()] = true;
                found = true;
              } else if (deq.back().get_vl()[0] == pll2.get_vl()[0] &&
                  deq.back().get_vl()[1] == pll2.get_vl()[1]) {
                Polyline polyl_new = pll2.get_pll();
                std::reverse(polyl_new.begin(), polyl_new.end());
                pll2.set_pll(polyl_new, pll2.get_pll_org());
                deq.push_back(pll2);
                visited[gd_num][pgnwh_num][pll2.get_pos()] = true;
                found = true;
              }
            }
            if (!found) break;
          }
          std::cout << "Sequence of polylines for: " << gd_num << " " << pgnwh_num << std::endl;
          Polygon outer_2;
          for (PLL pll_deq : deq) {
            std::cout << pll_deq.get_v1() << " " << pll_deq.get_vl() << " " << pll_deq.get_pos() << std::endl;
            for (Point pt : pll_deq.get_pll())
              outer_2.push_back(pt);
          }

          if (holes_v.empty()) {
            Polygon_with_holes pgnwh_final_2(outer_2);
            gd_final.push_back(pgnwh_final_2);
          } else {
            // Check if hole's middle vertex is inside boundary
            // std::cout << holes_v.size() << std::endl;
            bool holes_inside = true; 
            for (Polygon hole : holes_v)
              if (CGAL::bounded_side_2(outer_2.begin(), outer_2.end(), hole[hole.size() / 2]) != CGAL::ON_BOUNDED_SIDE)
                holes_inside = false;
            if (holes_inside) {
              Polygon_with_holes pgnwh_final_2(outer_2, holes_v.begin(), holes_v.end());
              gd_final.push_back(pgnwh_final_2);
              holes_v.clear();
            }
          }

          std::cout << std::endl;
        }

      }
    }
    container_final.push_back(gd_final);
  }
}

void print_num_pts(std::vector<GeoDiv> container) {
  int num_pts = 0;
  for (GeoDiv gd : container) {
    for (Polygon_with_holes pgnwh : gd.polygons_with_holes()) {
      Polygon outer = pgnwh.outer_boundary();
      num_pts += outer.size();

      std::vector<Polygon> holes_v(pgnwh.holes_begin(), pgnwh.holes_end());
      for (Polygon hole : holes_v)
        num_pts += hole.size();
    }
  }
  std::cout << num_pts << std::endl;
}

void repeat_first_point_as_last_point(std::vector<GeoDiv> &container) {
  for (GeoDiv &gd : container) {
    for (Polygon_with_holes &pgn_wh : *gd.ref_to_polygons_with_holes()) {
      Polygon_with_holes pgn_wh_new;

      Polygon *outer = &pgn_wh.outer_boundary();
      outer->push_back((*outer)[0]);

      std::vector<Polygon> holes_v(pgn_wh.holes_begin(), pgn_wh.holes_end());
      for (auto hole = pgn_wh.holes_begin(); hole != pgn_wh.holes_end(); hole++) {
        hole->push_back((*hole)[0]); 
      }
    }
  }
}

void remove_first_point_as_last_point(std::vector<GeoDiv> &container) {
  for (GeoDiv &gd : container) {
    for (Polygon_with_holes &pgn_wh : *gd.ref_to_polygons_with_holes()) {
      Polygon_with_holes pgn_wh_new;

      Polygon *outer = &pgn_wh.outer_boundary();
      auto outer_it = outer->end();
      outer_it--;
      outer->erase(outer_it);

      std::vector<Polygon> holes_v(pgn_wh.holes_begin(), pgn_wh.holes_end());
      for (auto hole = pgn_wh.holes_begin(); hole != pgn_wh.holes_end(); hole++) {
        auto hole_it = hole->end();
        hole_it--;
        hole->erase(hole_it);
      }
    }
  }
}

void simplify_map(MapState *map_state) {
  // Steps:
  // 1. Repeat first point as last point by reference 
  // 2. Create graph and split graph into unique polylines
  // 3. Densify
  // 4. Store polylines from polyline_list in CT
  // 5. Store ct polylines (densified) with their associated polylines (non-densified)
  // 6. Store polylines by positions with their associated GeoDivs and Polygon_with_holess
  // 7. Simplify polylines
  // 8. Store polylines by GeoDivs and Polygon_with_holess with their associated positions
  // 9. Set visited values
  // 10. Assemble polylines into polygons
  // 11. Remove first point as last point by reference 
  
  std::vector<GeoDiv> container = map_state->geo_divs();

  // 1. Repeat first point as last point by reference 
  repeat_first_point_as_last_point(container);

  // 2. Create graph and split graph into unique polylines
  Graph graph = create_pll_graph(container);
  std::list<Polyline> polyline_list;
  Polyline_visitor<Graph> polyline_visitor(polyline_list, graph);
  CGAL::split_graph_into_polylines(graph, polyline_visitor);

  // 3. Densify
  std::list<Polyline> polyline_list_dens = densify(polyline_list);

  // 4. Store polylines from polyline_list in CT
  CT ct; 
  for (Polyline polyline : polyline_list_dens) {
    ct.insert_constraint(polyline.begin(), polyline.end());
    std::cout << "inserted polyline" << std::endl;
  }

  // 5. Store ct polylines (densified) with their associated original polylines (non-densified)
  std::map<int, Polyline> pll_dens_to_org = store_polyline_dens_to_org(ct, polyline_list);

  // 6. Store polylines by positions with their associated GeoDivs and Polygon_with_holess
  std::cout << "Store polylines by positions with their associated GeoDivs and Polygon_with_holess" << std::endl;
  std::map<int, std::vector<PLL>> pll_cntr_by_pos = store_by_pos(ct, container, pll_dens_to_org);

  // 7. Simplify polylines
  PS::simplify(ct, Cost(), Stop(0.2));

  // 8. Store polylines by GeoDivs and Polygon_with_holess with their associated positions
  std::map<int, std::map<int, std::vector<PLL>>> pll_cntr_by_gd_pgnwh = store_by_gd_pgnwh(container, ct, pll_cntr_by_pos);

  // No longer needed
  // label_holes_correctly(container, pll_cntr_by_gd_pgnwh);

  // 9. Set visited values
  std::unordered_map<int, std::unordered_map<int, std::unordered_map<int, bool>>> visited;
  set_visited_vals(visited, pll_cntr_by_gd_pgnwh);

  std::cout << "Assemble polylines into polygons" << std::endl;
  // 10. Assemble polylines into polygons
  std::vector<GeoDiv> container_simp;
  assemble_pll_to_pgn(pll_cntr_by_gd_pgnwh, visited, container_simp);

  // Print number of points after simplifying
  std::cout << "Number of vertices before simplifying: ";
  print_num_pts(container);

  // Print number of points after simplifying
  std::cout << "Number of vertices after simplifying: ";
  print_num_pts(container_simp);

  // 11. Remove first point as last point by reference 
  remove_first_point_as_last_point(container_simp);

  map_state->set_geo_divs(container_simp);
}
