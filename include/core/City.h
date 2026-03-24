/**
 * @file City.h
 * @author Mohamed Elkeran
 * @license MIT (c) 2026
 */
#pragma once


struct City {
    int id;
    double x;
    double y;

    City() : id(0), x(0.0), y(0.0) {}
    City(int id, double x, double y) : id(id), x(x), y(y) {}
};
