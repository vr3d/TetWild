// This file is part of TetWild, a software for generating tetrahedral meshes.
// 
// Copyright (C) 2018 Yixin Hu <yixin.hu@nyu.edu>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
//
// Created by Yixin Hu on 3/29/17.
//

#include "DelaunayTetrahedralization.h"

#include <igl/readOFF.h>
#include <igl/readSTL.h>
#include <igl/readOBJ.h>
#include <igl/writeSTL.h>

#include <igl/unique.h>
#include <igl/unique_rows.h>
#include <igl/unique_simplices.h>
#include <geogram/mesh/mesh_AABB.h>
//#include <geogram/mesh/mesh.h>

#include <igl/boundary_loop.h>

void DelaunayTetrahedralization::init(const std::vector<Point_3>& m_vertices, const std::vector<std::array<int, 3>>& m_faces,
              std::vector<int>& m_f_tags, std::vector<int>& raw_e_tags, std::vector<std::vector<int>>& raw_conn_e4v) {
    m_f_tags.reserve(m_faces.size());
    for (int i = 0; i < m_faces.size(); i++)
        m_f_tags.push_back(i);

    std::vector<std::array<int, 2>> m_edges;
    for (int i = 0; i < m_faces.size(); i++) {
        for (int j = 0; j < 3; j++) {
            std::array<int, 2> e = {m_faces[i][j], m_faces[i][(j + 1) % 3]};
            if (e[0] > e[1])
                e = {e[1], e[0]};
            m_edges.push_back(e);
        }
    }
    std::sort(m_edges.begin(), m_edges.end());
    m_edges.erase(std::unique(m_edges.begin(), m_edges.end()), m_edges.end());

    raw_e_tags = std::vector<int>(m_edges.size(), -1);
    raw_conn_e4v = std::vector<std::vector<int>>(m_vertices.size(), std::vector<int>());
    for (int i = 0; i < m_edges.size(); i++) {
        for (int j = 0; j < 2; j++)
            raw_conn_e4v[m_edges[i][j]].push_back(i);
        raw_e_tags[i] = i;
    }
}

void DelaunayTetrahedralization::getVoxelPoints(const Point_3& p_min, const Point_3& p_max, GEO::Mesh& geo_surface_mesh,
                                                std::vector<Point_d>& voxel_points) {
    GEO::MeshFacetsAABB geo_face_tree(geo_surface_mesh);

    double voxel_resolution = g_diag_l / 20.0;
    std::array<double, 3> d;
    std::array<int, 3> N;
    for (int i = 0; i < 3; i++) {
        double D = CGAL::to_double(p_max[i] - p_min[i]);
        N[i] = (D / voxel_resolution) + 1;
        d[i] = D / N[i];
    }
    std::array<std::vector<CGAL_FT>, 3> ds;
    for (int i = 0; i < 3; i++) {
        ds[i].push_back(p_min[i]);
        for (int j = 0; j < N[i] - 1; j++) {
            ds[i].push_back(p_min[i] + d[i] * (j + 1));
        }
        ds[i].push_back(p_max[i]);
    }

    double min_dis = voxel_resolution * voxel_resolution / 4;
//    double min_dis = g_ideal_l * g_ideal_l;//epsilon*2
    for (int i = 0; i < ds[0].size(); i++) {
        for (int j = 0; j < ds[1].size(); j++) {
            for (int k = 0; k < ds[2].size(); k++) {
                if ((i == 0 || i == ds[0].size() - 1) && (j == 0 || j == ds[1].size() - 1)
                    && (k == 0 || k == ds[2].size() - 1))
                    continue;
                GEO::vec3 geo_p(CGAL::to_double(ds[0][i]), CGAL::to_double(ds[1][j]), CGAL::to_double(ds[2][k]));
                if (geo_face_tree.squared_distance(geo_p) < min_dis)
                    continue;
                voxel_points.push_back(Point_d(ds[0][i], ds[1][j], ds[2][k]));
            }
        }
    }
}

void DelaunayTetrahedralization::tetra(const std::vector<Point_3>& m_vertices, GEO::Mesh& geo_surface_mesh,
                                       std::vector<Point_3>& bsp_vertices, std::vector<BSPEdge>& bsp_edges,
                                       std::vector<BSPFace>& bsp_faces, std::vector<BSPtreeNode>& bsp_nodes) {
    std::vector<std::pair<Point_d, int>> points;
    const int m_vertices_size = m_vertices.size();
    points.reserve(m_vertices_size);
    for (int i = 0; i < m_vertices_size; i++) {
        points.push_back(std::make_pair(Point_d(m_vertices[i][0], m_vertices[i][1], m_vertices[i][2]), i));
    }

    ///add 8 virtual vertices
    Bbox_3 bbox = CGAL::bounding_box(m_vertices.begin(), m_vertices.end());
    Point_3 p_min = bbox.min();
    Point_3 p_max = bbox.max();

    double dis = g_eps * 2;//todo: use epsilon to determine the size of bbx
    if (dis < g_diag_l / 20)
        dis = g_diag_l / 20;
    else
        dis = g_eps * 1.1;
    p_min = Point_3(p_min[0] - dis, p_min[1] - dis, p_min[2] - dis);
    p_max = Point_3(p_max[0] + dis, p_max[1] + dis, p_max[2] + dis);

    cout<<"p_min: "<<CGAL::to_double(p_min[0])<<", "<<CGAL::to_double(p_min[1])<<", "<<CGAL::to_double(p_min[2])<<endl;
    cout<<"p_max: "<<CGAL::to_double(p_max[0])<<", "<<CGAL::to_double(p_max[1])<<", "<<CGAL::to_double(p_max[2])<<endl;

    for (int i = 0; i < 8; i++) {
        std::array<CGAL_FT, 3> p;
        std::bitset<sizeof(int) * 8> a(i);
        for (int j = 0; j < 3; j++) {
            if (a.test(j))
                p[j] = p_max[j];
            else
                p[j] = p_min[j];
        }
        points.push_back(std::make_pair(Point_d(p[0], p[1], p[2]), m_vertices_size + i));
    }
    ///add voxel points
    std::vector<Point_d> voxel_points;
    if(args.is_using_voxel)
        getVoxelPoints(p_min, p_max, geo_surface_mesh, voxel_points);
    for(int i=0;i<voxel_points.size();i++) {
        points.push_back(std::make_pair(voxel_points[i], m_vertices_size + 8 + i));
    }
    cout<<voxel_points.size()<<" voxel points are added!"<<endl;

    Delaunay T(points.begin(), points.end());
    if(!T.is_valid()){
        cout<<"T is not valid!!"<<endl;
        exit(250);
    }

    //////get nodes, faces, edges info
    //get bsp nodes
    int cnt = 0;
    std::vector<std::array<int, 4>> cells;
    cells.reserve(T.number_of_finite_cells());
    std::vector<std::vector<int>> conn_n_ids(points.size(), std::vector<int>());
    for (auto it = T.finite_cells_begin(); it != T.finite_cells_end(); ++it) {//it is determinate
        std::array<int, 4> c;
        for (int i = 0; i < 4; i++) {
            int n = it->vertex(i)->info();
            c[i] = n;
        }
        std::sort(c.begin(), c.end());
        cells.push_back(c);
    }
    std::sort(cells.begin(), cells.end());
    for(int i=0;i<cells.size();i++) {
        for (int j = 0; j < 4; j++)
            conn_n_ids[cells[i][j]].push_back(i);
    }

    //get bsp faces
    std::vector<std::array<int, 3>> faces;
    faces.reserve(T.number_of_finite_facets());
    std::vector<std::vector<int>> conn_f_ids(points.size(), std::vector<int>());
    for (auto it = T.finite_facets_begin(); it != T.finite_facets_end(); ++it) {
        std::array<int, 3> f;
        for (int i = 0; i < 3; i++) {
            int n = it->first->vertex((it->second + i + 1) % 4)->info();
            assert(!(points[n].first != it->first->vertex((it->second + i + 1) % 4)->point()));
            f[i] = n;
        }
        std::sort(f.begin(), f.end());
        faces.push_back(f);
    }
    std::sort(faces.begin(), faces.end());
    for(int i=0;i<faces.size();i++) {
        for (int j = 0; j < 3; j++)
            conn_f_ids[faces[i][j]].push_back(i);
    }

    //get bsp edges
    std::vector<std::array<int, 2>> edges;
    edges.reserve(T.number_of_finite_edges());
    for (auto it = T.finite_edges_begin(); it != T.finite_edges_end(); ++it) {
        std::array<int, 2> e;
        assert(!(points[it->first->vertex(it->second)->info()].first !=
                 it->first->vertex(it->second)->point()));
        assert(!(points[it->first->vertex(it->third)->info()].first !=
                 it->first->vertex(it->third)->point()));
        e[0] = it->first->vertex(it->second)->info();
        e[1] = it->first->vertex(it->third)->info();
        if(e[0]>e[1])
            e={e[1], e[0]};
        edges.push_back(e);
    }
    std::sort(edges.begin(), edges.end());

    //////construct bsp tree
    bsp_vertices.reserve(points.size());//+++
    bsp_vertices = m_vertices;
    for (int i = m_vertices_size; i < points.size(); i++) {
        bsp_vertices.push_back(Point_3(points[i].first[0], points[i].first[1], points[i].first[2]));
    }
    bsp_edges = std::vector<BSPEdge>(edges.size(), BSPEdge());
    bsp_faces = std::vector<BSPFace>(faces.size(), BSPFace());
    bsp_nodes = std::vector<BSPtreeNode>(cells.size(), BSPtreeNode());

    const int faces_size = faces.size();
    for (int i = 0; i < faces_size; i++) {
        std::array<int, 3> &f = faces[i];
        //vertices
        bsp_faces[i].vertices = {f[0], f[1], f[2]};
        //conn_nodes
        std::vector<int> tmp;
        //no need to sort before intersection because elements have been sorted
        std::set_intersection(conn_n_ids[f[0]].begin(), conn_n_ids[f[0]].end(),
                              conn_n_ids[f[1]].begin(), conn_n_ids[f[1]].end(),
                              std::back_inserter(tmp));
        std::set_intersection(tmp.begin(), tmp.end(),
                              conn_n_ids[f[2]].begin(), conn_n_ids[f[2]].end(),
                              std::inserter(bsp_faces[i].conn_nodes, bsp_faces[i].conn_nodes.begin()));
        //faces for nodes
        for (auto it = bsp_faces[i].conn_nodes.begin(); it != bsp_faces[i].conn_nodes.end(); it++) {
            bsp_nodes[*it].faces.push_back(i);
        }
    }

    const int edges_size = edges.size();
    for (int i = 0; i < edges_size; i++) {
        std::array<int, 2> &e = edges[i];
        //vertices
        bsp_edges[i].vertices = {e[0], e[1]};
        //conn_faces
        std::set_intersection(conn_f_ids[e[0]].begin(), conn_f_ids[e[0]].end(),
                              conn_f_ids[e[1]].begin(), conn_f_ids[e[1]].end(),
                              std::inserter(bsp_edges[i].conn_faces, bsp_edges[i].conn_faces.begin()));
        //edges for faces
        for (auto it = bsp_edges[i].conn_faces.begin(); it != bsp_edges[i].conn_faces.end(); it++) {
            bsp_faces[*it].edges.push_back(i);
        }
    }
}

void DelaunayTetrahedralization::outputTetmesh(const std::vector<Point_3>& m_vertices, std::vector<std::array<int, 4>>& cells,
                                               const std::string& output_file){
    std::streambuf* coutBuf = std::cout.rdbuf();
    std::ofstream of(output_file);
    std::cout.rdbuf(of.rdbuf());

    cout<<m_vertices.size()<<" "<<cells.size()<<endl;
    for(int i=0;i<m_vertices.size();i++){
        cout<<m_vertices[i]<<endl;
    }
    for(int i=0;i<cells.size();i++){
        for(int j=0;j<4;j++)
            cout<<cells[i][j]<<" ";
        cout<<endl;
    }

    of.flush();
    of.close();
    std::cout.rdbuf(coutBuf);
}
