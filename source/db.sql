drop database if exists Rokuko;
create database if not exists Rokuko;
use Rokuko;
create table if not exists user(
    id int primary key auto_increment,  
    username varchar(32) unique key not null,      
    password varchar(32) not null,      
    socre int,                          
    total_count int,                    
    win_count int                       
);