;;; hedgr-prodigy.el --- Sets up prodigy for this project.  -*- lexical-binding: t; -*-

;; Copyright (C) 2025  Marco Craveiro

;; Author: Marco Craveiro <marco.craveiro@gmail.com>
;; Keywords: local

;; This program is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.

;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with this program.  If not, see <https://www.gnu.org/licenses/>.

;;; Commentary:

;;
;;; Code:
(require 'project)
(require 'cl-lib)
(require 'prodigy)

(autoload 'prodigy-define-tag "prodigy")
(autoload 'prodigy-define-service "prodigy")
(defvar prodigy-services)

(defconst hedgr/checkout-label
  (let* ((pr (project-current t))
         (root (directory-file-name (expand-file-name (project-root pr))))
         (dir-name (file-name-nondirectory root)))
    (if (string-prefix-p "Hedgr." dir-name)
        (substring dir-name (length "Hedgr."))
      dir-name))
  "The checkout label derived from the project directory name.")

(defconst hedgr/checkout-tag (intern hedgr/checkout-label)
  "The tag symbol for the current checkout.")

;; Remove existing services for THIS checkout only, to allow reloading without
;; duplicates while preserving services from other checkouts.
(when (boundp 'prodigy-services)
  (setq prodigy-services
        (cl-delete-if (lambda (service)
                        (memq hedgr/checkout-tag (plist-get service :tags)))
                      prodigy-services)))

(defun hedgr--get-build-output-path (build-type)
  "Return the path to the build output directory for BUILD-TYPE.
BUILD-TYPE should be either 'debug or 'release.
This is an internal helper function."
  (let* ((pr (project-current t))
         (root (expand-file-name (project-root pr)))
         (build-dir (if (eq build-type 'release)
                        "linux-clang-release"
                      "linux-clang-debug")))
    (concat root "build/output/" build-dir)))

(defun hedgr/path-to-publish (build-type)
  "Return the path to the publish directory for BUILD-TYPE.
BUILD-TYPE should be either 'debug or 'release."
  (concat (hedgr--get-build-output-path build-type) "/publish"))

(defun hedgr/service-name (base config)
  "Generate unique service name from BASE, CONFIG (debug/release) and checkout."
  (let ((cfg-char (if (eq config 'debug) "D" "R")))
    (format "%s [%s:%s]" base cfg-char hedgr/checkout-label)))

(prodigy-define-tag :name 'hedgr)
(prodigy-define-tag :name hedgr/checkout-tag)

(prodigy-define-service
  :name (hedgr/service-name "Hedgr ImGui" 'debug)
  :cwd (concat (hedgr/path-to-publish 'debug) "/bin")
  :args '("--log-enabled" "--log-level" "trace" "--log-directory" "../log")
  :command (concat (hedgr/path-to-publish 'debug) "/bin/hedgr.imgui")
  :tags `(hedgr ui debug ,hedgr/checkout-tag)
  :stop-signal 'sigint
  :kill-process-buffer-on-stop t)

(prodigy-define-service
  :name (hedgr/service-name "Hedgr ImGui" 'release)
  :cwd (concat (hedgr/path-to-publish 'release) "/bin")
  :args '("--log-enabled" "--log-level" "trace" "--log-directory" "../log")
  :command (concat (hedgr/path-to-publish 'release) "/bin/hedgr.imgui")
  :tags `(hedgr ui release ,hedgr/checkout-tag)
  :stop-signal 'sigint
  :kill-process-buffer-on-stop t)

(provide 'hedgr-prodigy)
;;; hedgr-prodigy.el ends here
